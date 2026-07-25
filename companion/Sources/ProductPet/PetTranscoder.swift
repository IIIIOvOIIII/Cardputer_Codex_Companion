import CoreGraphics
import Foundation
import ImageIO
import ProductContracts

public enum PetTranscoderError: Error {
    case decode
    case invalidAtlas
    case crop
    case context
}

public struct PetTranscoder {
    private static let stateRows: [PetState: Int] = [
        .idle: 0,
        .working: 7,
        .waiting: 6,
        .review: 8,
        .failed: 5
    ]

    private let backgroundRGB888: UInt32

    public init(backgroundRGB888: UInt32 = 0x05080d) {
        self.backgroundRGB888 = backgroundRGB888
    }

    public func transcode(_ source: PetSource) throws -> PetBundle {
        guard let imageSource = CGImageSourceCreateWithURL(
            source.atlasURL as CFURL,
            nil
        ), let image = CGImageSourceCreateImageAtIndex(
            imageSource,
            0,
            nil
        ) else {
            throw PetTranscoderError.decode
        }
        let expected = PetAtlas.expectedDimensions(version: source.atlasVersion)
        guard image.width == expected.0, image.height == expected.1 else {
            throw PetTranscoderError.invalidAtlas
        }
        var states: [PetState: [[UInt16]]] = [:]
        for state in PetState.allCases {
            guard let row = Self.stateRows[state] else {
                throw PetTranscoderError.invalidAtlas
            }
            states[state] = try (0..<8).map { column in
                try renderFrame(image: image, column: column, row: row)
            }
        }
        return try PetBundleEncoder.encode(petID: source.id, frames: states)
    }

    private func renderFrame(
        image: CGImage,
        column: Int,
        row: Int
    ) throws -> [UInt16] {
        let sourceRect = CGRect(
            x: column * PetAtlas.cellWidth,
            y: row * PetAtlas.cellHeight,
            width: PetAtlas.cellWidth,
            height: PetAtlas.cellHeight
        )
        guard let cropped = image.cropping(to: sourceRect) else {
            throw PetTranscoderError.crop
        }
        let width = 96
        let height = 104
        var rgba = [UInt8](repeating: 0, count: width * height * 4)
        guard let context = CGContext(
            data: &rgba,
            width: width,
            height: height,
            bitsPerComponent: 8,
            bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue |
                CGImageAlphaInfo.premultipliedLast.rawValue
        ) else {
            throw PetTranscoderError.context
        }
        let red = CGFloat((backgroundRGB888 >> 16) & 0xff) / 255
        let green = CGFloat((backgroundRGB888 >> 8) & 0xff) / 255
        let blue = CGFloat(backgroundRGB888 & 0xff) / 255
        context.setFillColor(
            CGColor(
                red: red,
                green: green,
                blue: blue,
                alpha: 1
            )
        )
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        context.interpolationQuality = .none
        let scale = min(
            CGFloat(width) / CGFloat(cropped.width),
            CGFloat(height) / CGFloat(cropped.height),
            1
        )
        let drawWidth = CGFloat(cropped.width) * scale
        let drawHeight = CGFloat(cropped.height) * scale
        let drawRect = CGRect(
            x: (CGFloat(width) - drawWidth) / 2,
            y: (CGFloat(height) - drawHeight) / 2,
            width: drawWidth,
            height: drawHeight
        )
        context.draw(cropped, in: drawRect)
        return stride(from: 0, to: rgba.count, by: 4).map { offset in
            let r = UInt16(rgba[offset])
            let g = UInt16(rgba[offset + 1])
            let b = UInt16(rgba[offset + 2])
            return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)
        }
    }
}
