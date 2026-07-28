import json
import re
from pathlib import Path


SOURCE = Path("firmware/main/product/product_web.cpp")
CODEC_SOURCE = Path("firmware/main/product/profile_codec.cpp")
CATALOG_SOURCE = Path("firmware/main/product/profile_catalog.cpp")
CONTROLLER_SOURCE = Path("firmware/main/product/product_controller.cpp")
SDKCONFIG_DEFAULTS = Path("firmware/sdkconfig.defaults")


def test_sparse_default_profile_fits_nvs_string_limit() -> None:
    payload = {
        "name": "SAFE",
        "revision": 1,
        "bindings": [None] * 224,
    }
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode()
    assert len(encoded) == 1161
    assert len(encoded) < 4000


def test_product_web_uses_sparse_null_and_atomic_persistence() -> None:
    source = SOURCE.read_text()
    codec = CODEC_SOURCE.read_text()
    encoder = codec[
        codec.index("ProfileCodecResult encode_profile")
        : codec.index("ProfileCodecResult decode_profile")
    ]
    assert 'encoder.literal("null")' in encoder
    assert "cJSON_CreateObject()" not in encoder
    assert "cJSON_PrintUnformatted" not in encoder
    assert "JsonEncoder counter(nullptr);" in encoder
    assert "output.clear();" in encoder
    assert "catch (const std::bad_alloc&)" in encoder
    assert "cJSON_IsNull(item)" in codec
    assert "esp_err_t persist_profile(" in source
    assert '"{\\"error\\":\\"profile_persist_failed\\"}"' in source
    persist = source.index("persist_profile(json.c_str())")
    activate = source.rindex("g_profile = std::move(*candidate)")
    assert persist < activate


def test_legacy_profile_load_does_not_allocate_second_full_profile() -> None:
    source = SOURCE.read_text()
    loader = source[
        source.index("void load_profile()")
        : source.index("std::optional<std::string> legacy_profile_json()")
    ]
    assert "std::make_unique<Profile>()" not in loader
    assert "decode_profile(json, g_profile)" in loader
    assert "g_profile = {};" not in loader


def test_catalog_activation_reuses_runtime_profile() -> None:
    source = SOURCE.read_text()
    activation = source[
        source.index("bool activate_catalog_profile")
        : source.index("void set_pairing_code")
    ]
    assert "std::make_unique<Profile>()" not in activation
    assert "g_profile_catalog->read(id, g_profile)" in activation


def test_profile_put_does_not_place_full_profile_on_https_task_stack() -> None:
    source = SOURCE.read_text()
    handler = source[
        source.index("esp_err_t put_profile_handler")
        : source.index("esp_err_t list_profiles_handler")
    ]
    assert "std::make_unique<Profile>()" not in handler
    assert "allocate_profile()" in handler
    assert handler.count("allocate_profile()") == 1
    assert "auto saved =" not in handler
    assert '"{\\"error\\":\\"profile_memory_unavailable\\"}"' in source
    assert "Profile candidate;" not in handler
    assert "output = safe_profile();" not in source
    assert "Profile loaded;" not in source


def test_active_profile_get_uses_resident_profile_under_tls_pressure() -> None:
    source = SOURCE.read_text()
    handler = source[
        source.index("esp_err_t get_profile_handler")
        : source.index("esp_err_t put_profile_handler")
    ]
    assert "const std::string id = requested_or_active_profile_id(request);" in handler
    assert "id == g_profile_catalog->active_id()" in handler
    assert "encode_profile(g_profile, json)" in handler
    assert handler.index("encode_profile(g_profile, json)") < handler.index(
        "allocate_profile()"
    )
    assert "std::make_unique<Profile>()" not in handler


def test_runtime_profile_temporaries_are_heap_allocated() -> None:
    sources = {
        "product_web.cpp": SOURCE.read_text(),
        "profile_codec.cpp": CODEC_SOURCE.read_text(),
        "profile_catalog.cpp": CATALOG_SOURCE.read_text(),
    }
    stack_profile = re.compile(
        r"^\s+Profile\s+[A-Za-z_][A-Za-z0-9_]*\s*(?:[;=])",
        re.MULTILINE,
    )

    for name, source in sources.items():
        assert not stack_profile.search(source), (
            f"{name} places a full Profile on a constrained ESP task stack"
        )

    assert "std::make_unique<Profile>()" not in sources["profile_codec.cpp"]
    assert "output.name = name->valuestring;" in sources["profile_codec.cpp"]
    assert "output = {};" not in sources["profile_codec.cpp"]
    assert "reset_profile(output);" in sources["profile_codec.cpp"]
    assert "std::make_unique<Profile>()" not in sources["profile_catalog.cpp"]
    assert "output = safe_profile();" not in sources["profile_catalog.cpp"]
    assert "reset_to_safe_profile(output);" in sources["profile_catalog.cpp"]
    assert sources["product_web.cpp"].count("g_profile = safe_profile();") == 1
    assert "std::unique_ptr<Profile> scratch_;" in Path(
        "firmware/main/product/profile_catalog.hpp"
    ).read_text()


def test_profile_catalog_flash_mutations_run_on_worker() -> None:
    source = CATALOG_SOURCE.read_text()
    read = source[
        source.index("bool EspProfileCatalogBackend::read")
        : source.index("bool EspProfileCatalogBackend::erase")
    ]
    erase = source[
        source.index("bool EspProfileCatalogBackend::erase")
        : source.index("bool EspProfileCatalogBackend::write")
    ]

    assert "xTaskCreateStatic(" in source
    assert "xTaskCreateStaticPinnedToCore(" not in source
    assert "std::array<StackType_t, 2048> storage_task_stack{};" in source
    assert "this, tskIDLE_PRIORITY, impl_->storage_task_stack.data()," in source
    assert "storage_task" in source
    assert "submit_storage_command" in source
    assert "submit_storage_command(kCommandRead)" in read
    assert "esp_partition_read(" not in read
    assert "submit_storage_command(kCommandErase)" in erase
    assert "esp_partition_erase_range(" not in erase
    assert "vTaskDelay(pdMS_TO_TICKS(10))" in source
    assert "std::array<uint8_t, kChunkBytes> chunk{};" not in source
    assert "allocate_catalog_chunk()" in source
    assert "new (std::nothrow) CatalogChunk()" in source
    assert "ProfileCatalogStore::publish(" in source
    assert "Profile& profile," in source
    assert "&profile);" in source


def test_flash_ipc_uses_esp_idf_caller_priority() -> None:
    defaults = SDKCONFIG_DEFAULTS.read_text()
    assert "CONFIG_ESP_IPC_USES_CALLERS_PRIORITY=y" in defaults


def test_profile_catalog_migration_finishes_before_network_services() -> None:
    source = CONTROLLER_SOURCE.read_text()
    config = source[
        source.index("bool config() override")
        : source.index("bool keyboard() override")
    ]
    companion = source[
        source.index("bool companion() override")
        : source.index("\n private:", source.index("bool companion() override"))
    ]
    assert "product_web_prepare_profile_catalog" not in config
    assert "profile_catalog_task" in source
    assert "g_profile_catalog_task_stack" not in source
    assert "xTaskCreate(profile_catalog_task" in config
    assert "32768" in config
    assert "xSemaphoreTake(" in config
    assert "g_profile_catalog_initialization_done," in config
    assert "portMAX_DELAY" in config
    assert "xTaskCreate(profile_catalog_task" not in companion
    task = source[
        source.index("void profile_catalog_task")
        : source.index("\nclass EspProductStartup")
    ]
    assert "product_wifi_state()" not in task
    assert "xSemaphoreGive(g_profile_catalog_initialization_done)" in task
    assert "vTaskDelete(nullptr)" in task


def test_web_tls_identity_waits_for_wifi_flash_activity() -> None:
    source = CONTROLLER_SOURCE.read_text()
    web = source[
        source.index("bool web() override")
        : source.index("bool companion() override")
    ]
    wait = web.index("product_wifi_state()")
    catalog_wait = web.index("g_profile_catalog_initialization_complete")
    start = web.index("product_web_start()")
    assert wait < start
    assert catalog_wait < start
    assert "WifiState::candidate_connecting" in web
    assert "WifiState::rollback_connecting" in web


def test_https_caps_open_connections_for_tls_heap_budget() -> None:
    source = SOURCE.read_text()
    start = source[source.index("esp_err_t product_web_start()") :]
    assert "config.httpd.max_open_sockets = 3;" in start
    assert "config.httpd.lru_purge_enable = true;" in start
    assert "CONFIG_MBEDTLS_DYNAMIC_BUFFER=y" in SDKCONFIG_DEFAULTS.read_text()


def test_profile_catalog_releases_transaction_scratch() -> None:
    source = CATALOG_SOURCE.read_text()
    assert "finish_transaction" in source
    commit = source[source.index("ProfileCatalogResult ProfileCatalogStore::commit") :]
    assert "return finish_transaction(" in commit
