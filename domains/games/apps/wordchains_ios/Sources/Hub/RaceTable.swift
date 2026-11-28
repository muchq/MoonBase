import Foundation

/// What the race table screen decides, from the hub's view and who is
/// looking: what to say, what the start button starts, when the typed word
/// clears. RaceTableView only draws it.
struct RaceTable {
    let view: WordchainView
    let viewer: String

    var me: WordchainRunner? { view.runners.first { $0.playerId == viewer } }
    var ladder: [String] { me?.ladder ?? [] }
    var racing: Bool { view.phase == .racing }
    /// Seated and still climbing this race.
    var climbing: Bool { racing && me != nil && me?.finishedAtMs == nil }
    /// A seat that left keeps its runner but is no company.
    var seated: Int { view.runners.filter { !$0.left }.count }

    var title: String { view.isPractice ? "Practice · \(view.gameId)" : "Word chains · \(view.gameId)" }
    var showsWins: Bool { !view.isPractice }

    /// Alone, a start is practice (#1730); with company, a race.
    var startLabel: String {
        if seated < 2 { return view.phase == .over ? "Practice again" : "Practice" }
        return view.phase == .over ? "Next race" : "Start the race"
    }

    /// The next race offers the last one's letters.
    var lastLetters: Int? { view.start?.count }

    var hint: String? {
        switch view.phase {
        case .waiting:
            return seated < 2 ? "Practice alone, or wait for someone to join." : "Start when everyone’s here."
        case .racing where climbing && view.isPractice:
            return "Practice: one letter at a time. Anyone who joins ends it."
        case .racing where climbing:
            return "One letter at a time, every rung a word."
        case .racing where me != nil:
            return "You reached \(view.target ?? "") in \(RaceText.steps(ladder.count)). Waiting for the others."
        case .over where view.isPractice:
            return "Practice again, or wait for someone to join."
        case .over:
            return "Anyone can start the next race."
        default:
            return nil
        }
    }

    /// A practice race has nobody to beat; a real one, a winner.
    func said(_ result: WordchainResult) -> String {
        view.isPractice
            ? RaceText.practice(result, runners: view.runners, viewer: viewer, par: view.par)
            : RaceText.result(result, runners: view.runners, viewer: viewer)
    }

    /// Only changes to the viewer's climb within the same race make a rung sound.
    func sounds(since previous: WordchainView) -> [GameSound] {
        guard previous.phase == .racing, previous.gameId == view.gameId,
              previous.startedAtMs == view.startedAtMs,
              let before = RaceTable(view: previous, viewer: viewer).me,
              let after = me else { return [] }

        var sounds: [GameSound] = []
        if after.words > before.words {
            sounds.append(after.finishedAtMs == nil ? .step(after.words) : .target(after.words))
        } else if let cue = GameSound.takeback(from: before.words, to: after.words) {
            sounds.append(cue)
        }
        if previous.result == nil, let result = view.result {
            if !view.isPractice, result.winners.contains(viewer) {
                sounds.append(result.winners.count > 1 ? .sharedWin : .win)
            } else if result.ending == .timeout {
                sounds.append(.timeout)
            } else if result.ending == .finished, !view.isPractice {
                sounds.append(.raceFinished)
            }
        }
        return sounds
    }

    /// A shortest ladder to show once the race is over: never while it
    /// runs, since it would show the way.
    var solution: [String]? {
        guard !racing, let ladder = view.solution, !ladder.isEmpty else { return nil }
        return ladder
    }

    /// Names the race in play, so a ladder shown for one hides at the next.
    var raceKey: String { "\(view.start ?? "")|\(view.target ?? "")|\(view.startedAtMs ?? 0)" }

    /// Changes when the viewer's rung lands or is taken back, or a new race
    /// starts, and only then: the typed word clears on a change, so a
    /// refused word stays to be fixed.
    var draftKey: String { "\(raceKey)#\(ladder.count)" }

    /// No word is longer than the puzzle's.
    func capped(_ typed: String) -> String {
        guard let length = view.start?.count, typed.count > length else { return typed }
        return String(typed.prefix(length))
    }

    /// Leaving a race in play gives it up, so it is asked twice.
    var asksBeforeLeaving: Bool { racing && me != nil }
}
