import SwiftUI
#if canImport(UIKit)
import UIKit
#elseif canImport(AppKit)
import AppKit
#endif

enum Theme {
    // MARK: - Colors

    static let accent = adaptive(light: 0x3347BA, dark: 0xAAB4FF)

    static let primaryFill = rgb(0x3C51BE)
    static let gradientEnd = rgb(0x6635A0)

    static let chainStart = adaptive(light: 0x146747, dark: 0x65D9A2)
    static let chainEnd = accent

    static let errorRed = adaptive(light: 0xB42335, dark: 0xFF7E88)
    static let warningOrange = adaptive(light: 0x89500C, dark: 0xFFB660)
    static let hintBlue = adaptive(light: 0x215FAC, dark: 0x79B4FF)

    #if canImport(UIKit)
    static let cardBackground = Color(.systemBackground)
    static let secondaryBackground = Color(.secondarySystemBackground)
    static let tertiaryBackground = Color(.tertiarySystemBackground)
    #else
    static let cardBackground = Color(.windowBackgroundColor)
    static let secondaryBackground = Color(.controlBackgroundColor)
    static let tertiaryBackground = Color(.textBackgroundColor)
    #endif

    static let actionGradient = LinearGradient(
        colors: [primaryFill, gradientEnd], startPoint: .leading, endPoint: .trailing
    )

    private static func rgb(_ value: UInt32) -> Color {
        Color(
            red: Double((value >> 16) & 0xFF) / 255,
            green: Double((value >> 8) & 0xFF) / 255,
            blue: Double(value & 0xFF) / 255
        )
    }

    private static func adaptive(light: UInt32, dark: UInt32) -> Color {
        #if canImport(UIKit)
        Color(UIColor { traits in UIColor(rgb(traits.userInterfaceStyle == .dark ? dark : light)) })
        #else
        Color(NSColor(name: nil) { appearance in
            NSColor(rgb(appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? dark : light))
        })
        #endif
    }

    // MARK: - Typography

    static let titleFont = Font.system(.largeTitle, design: .rounded).weight(.bold)
    static let headlineFont = Font.system(.title2, design: .rounded).weight(.semibold)
    static let bodyFont = Font.system(.body, design: .rounded)
    static let captionFont = Font.system(.footnote, design: .rounded).weight(.medium)
    static let tileFont = Font.system(size: 28, weight: .bold, design: .monospaced)
    static let smallTileFont = Font.system(size: 22, weight: .bold, design: .monospaced)

    // MARK: - Layout

    static let cornerRadius: CGFloat = 16
    static let tileSize: CGFloat = 48
    static let tileSpacing: CGFloat = 6
    static let cardPadding: CGFloat = 20
    static let sectionSpacing: CGFloat = 24

    // MARK: - Animations

    static let springAnimation = Animation.spring(response: 0.4, dampingFraction: 0.7)
    static let quickSpring = Animation.spring(response: 0.25, dampingFraction: 0.8)
    static let bounceAnimation = Animation.spring(response: 0.5, dampingFraction: 0.5)
}

struct PrimaryActionStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(.headline, design: .rounded))
            .foregroundStyle(.white)
            .multilineTextAlignment(.center)
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .frame(maxWidth: .infinity, minHeight: 52)
            .background(Theme.actionGradient, in: RoundedRectangle(cornerRadius: Theme.cornerRadius))
            .opacity(isEnabled ? (configuration.isPressed ? 0.78 : 1) : 0.5)
    }
}

// MARK: - View Modifiers

struct CardStyle: ViewModifier {
    func body(content: Content) -> some View {
        content
            .padding(Theme.cardPadding)
            .background(Theme.cardBackground)
            .clipShape(RoundedRectangle(cornerRadius: Theme.cornerRadius, style: .continuous))
            .shadow(color: .black.opacity(0.08), radius: 12, y: 4)
    }
}

struct ShakeEffect: GeometryEffect {
    var amount: CGFloat = 8
    var shakesPerUnit = 3
    var animatableData: CGFloat

    func effectValue(size: CGSize) -> ProjectionTransform {
        ProjectionTransform(
            CGAffineTransform(translationX: amount * sin(animatableData * .pi * CGFloat(shakesPerUnit)), y: 0)
        )
    }
}

extension View {
    func cardStyle() -> some View {
        modifier(CardStyle())
    }

    func shakeEffect(trigger: Bool) -> some View {
        modifier(ShakeEffect(animatableData: trigger ? 1 : 0))
    }
}
