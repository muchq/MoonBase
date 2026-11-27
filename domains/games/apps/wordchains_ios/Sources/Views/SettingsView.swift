import SwiftUI

struct SettingsView: View {
    @Binding var soundEffectsEnabled: Bool
    @Binding var hapticsEnabled: Bool
    @Binding var prefersReducedMotion: Bool
    @Environment(\.accessibilityReduceMotion) private var systemReduceMotion

    var body: some View {
        Form {
            Section {
                Toggle("Sound Effects", isOn: $soundEffectsEnabled)
                    .accessibilityIdentifier("settings-sound-effects")
            } footer: {
                Text("Plays short sounds for words and races. Follows your device’s silent mode.")
            }
            Section {
                Toggle("Haptics", isOn: $hapticsEnabled)
                    .accessibilityIdentifier("settings-haptics")
            } footer: {
                Text("Feel feedback for incorrect words and solves.")
            }

            Section {
                Toggle("Reduce Motion", isOn: systemReduceMotion ? .constant(true) : $prefersReducedMotion)
                    .disabled(systemReduceMotion)
                    .accessibilityIdentifier("settings-reduce-motion")
            } footer: {
                if systemReduceMotion {
                    Text("Reduce Motion is on in your device’s accessibility settings.")
                } else {
                    Text("Reduces game animations such as shaking and celebrations.")
                }
            }
        }
    }
}
