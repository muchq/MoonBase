import SwiftUI

enum Theme {
    // MARK: - Colors

    static let accent = Color("AccentColor")

    static let gradientStart = Color(red: 0.35, green: 0.47, blue: 1.0)
    static let gradientEnd = Color(red: 0.64, green: 0.38, blue: 1.0)

    static let chainStart = Color(red: 0.30, green: 0.85, blue: 0.60)
    static let chainEnd = Color(red: 0.35, green: 0.47, blue: 1.0)

    static let validGreen = Color(red: 0.25, green: 0.78, blue: 0.55)
    static let errorRed = Color(red: 0.95, green: 0.30, blue: 0.35)
    static let warningOrange = Color(red: 1.0, green: 0.62, blue: 0.25)
    static let hintBlue = Color(red: 0.40, green: 0.65, blue: 1.0)

    static let cardBackground = Color(.systemBackground)
    static let secondaryBackground = Color(.secondarySystemBackground)
    static let tertiaryBackground = Color(.tertiarySystemBackground)

    static var headerGradient: LinearGradient {
        LinearGradient(colors: [gradientStart, gradientEnd], startPoint: .leading, endPoint: .trailing)
    }

    static func chainGradient(progress: Double) -> Color {
        let r = chainStart.components.red + (chainEnd.components.red - chainStart.components.red) * progress
        let g = chainStart.components.green + (chainEnd.components.green - chainStart.components.green) * progress
        let b = chainStart.components.blue + (chainEnd.components.blue - chainStart.components.blue) * progress
        return Color(red: r, green: g, blue: b)
    }

    // MARK: - Typography

    static let titleFont = Font.system(size: 34, weight: .bold, design: .rounded)
    static let headlineFont = Font.system(size: 22, weight: .semibold, design: .rounded)
    static let bodyFont = Font.system(size: 17, weight: .regular, design: .rounded)
    static let captionFont = Font.system(size: 13, weight: .medium, design: .rounded)
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

// MARK: - Color Extensions

extension Color {
    var components: (red: Double, green: Double, blue: Double, opacity: Double) {
        var r: CGFloat = 0
        var g: CGFloat = 0
        var b: CGFloat = 0
        var o: CGFloat = 0
        UIColor(self).getRed(&r, green: &g, blue: &b, alpha: &o)
        return (Double(r), Double(g), Double(b), Double(o))
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
