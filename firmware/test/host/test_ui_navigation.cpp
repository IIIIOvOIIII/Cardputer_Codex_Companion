#include <cassert>

#include "product/ui_model.hpp"
#include "product/ui_navigation.hpp"

int main() {
  assert(ui_page_allows_host_input(UiPage::pet));
  assert(!ui_page_allows_host_input(UiPage::device_status));
  assert(!ui_page_allows_host_input(UiPage::codex_status));
  assert(!ui_page_allows_host_input(UiPage::sync_status));
  assert(!ui_page_allows_host_input(UiPage::settings));
  assert(!ui_page_allows_host_input(UiPage::onboarding));

  UiNavigation navigation;
  auto result = navigation.on_key(39, true, false);
  assert(!result.captured);

  result = navigation.on_key(39, true, true);
  assert(result.captured);
  assert(result.action == UiNavAction::scroll_up);
  result = navigation.on_key(39, false, false);
  assert(result.captured);
  assert(result.action == UiNavAction::none);

  assert(navigation.on_key(52, true, true).action ==
         UiNavAction::previous_page);
  assert(navigation.on_key(52, false, true).captured);
  assert(navigation.on_key(53, true, true).action ==
         UiNavAction::scroll_down);
  assert(navigation.on_key(53, false, true).captured);
  assert(navigation.on_key(54, true, true).action ==
         UiNavAction::next_page);
  assert(navigation.on_key(54, false, false).captured);
  assert(!navigation.on_key(15, true, true).captured);

  result = navigation.on_key(
      39, true, false, UiInteractionContext::settings_browse);
  assert(result.captured);
  assert(result.action == UiNavAction::none);
  assert(navigation.on_key(
      39, false, false, UiInteractionContext::settings_browse).captured);
  assert(!navigation.on_key(
      39, true, false, UiInteractionContext::normal).captured);

  result = navigation.on_return_key(0, true, true);
  assert(result.captured);
  assert(result.action == UiNavAction::return_to_pet);
  result = navigation.on_return_key(0, false, false);
  assert(result.captured);
  assert(result.action == UiNavAction::none);
  assert(!navigation.on_return_key(0, true, false).captured);
  assert(!navigation.on_return_key(15, true, true).captured);
  return 0;
}
