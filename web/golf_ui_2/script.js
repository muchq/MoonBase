// script.js
// Card data
const SUITS = ['♠', '♥', '♦', '♣'];
const RANKS = ['A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K'];
const CARD_VALUES = {
    'A': 1, '2': 2, '3': 3, '4': 4, '5': 5, '6': 6, '7': 7, '8': 8, '9': 9, '10': 10,
    'J': 10, 'Q': 10, 'K': 10
};

// Game state
let gameState = {
    players: [
        { name: "Player 1", cards: [], score: 0, revealedCards: [] },
        { name: "Player 2", cards: [], score: 0, revealedCards: [] }
    ],
    drawPile: [],
    discardPile: [],
    currentPlayerIndex: 0,
    selectedCardIndex: null,
    potentialReplacement: null,
    gameEnded: false,
    knockedPlayer: null,
    roundsPlayed: 0
};

// DOM Elements
const elements = {
    notification: document.getElementById('notification'),
    drawCount: document.getElementById('draw-count'),
    discardCount: document.getElementById('discard-count'),
    playerCards: Array.from({ length: 4 }, (_, i) => document.getElementById(`card${i + 1}`)),
    drawCard: document.getElementById('draw-card'),
    discardCard: document.getElementById('discard-card'),
    playerNames: document.getElementById('player-names'),
    turnIndicator: document.getElementById('turn-indicator'),
    knockStatus: document.getElementById('knock-status'),
    playerScores: [
        document.getElementById('player1-score'),
        document.getElementById('player2-score')
    ],
    themeToggle: document.getElementById('theme-toggle-btn'),
    rulesModal: document.getElementById('rules-modal'),
    closeModalBtn: document.querySelector('.close-button'),
    rulesBtn: document.getElementById('rules-button'),
    newGameBtn: document.getElementById('new-game-button')
};

// Initialize the game
const initGame = () => {
    // Create a full deck of cards
    gameState.drawPile = [];
    for (const suit of SUITS) {
        for (const rank of RANKS) {
            gameState.drawPile.push({ rank, suit });
        }
    }

    // Shuffle the deck
    shuffle(gameState.drawPile);

    // Reset game state
    gameState.discardPile = [];
    gameState.currentPlayerIndex = 0;
    gameState.selectedCardIndex = null;
    gameState.potentialReplacement = null;
    gameState.gameEnded = false;
    gameState.knockedPlayer = null;

    // Deal cards to players
    for (let i = 0; i < gameState.players.length; i++) {
        gameState.players[i].cards = gameState.drawPile.splice(-4);
        gameState.players[i].revealedCards = [];
    }

    // Start discard pile with one card
    gameState.discardPile.push(gameState.drawPile.pop());

    // Update the display
    updateDisplay();
    updateStatusBar();
    updateScores();

    // Show notification
    showNotification('New game started! Each player can peek at two cards.');
};

// Shuffle the deck using Fisher-Yates algorithm
const shuffle = (array) => {
    for (let i = array.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [array[i], array[j]] = [array[j], array[i]];
    }
};

// Show notification instead of alert
const showNotification = (message, isError = false) => {
    elements.notification.textContent = message;
    elements.notification.classList.remove('hidden', 'error');

    if (isError) {
        elements.notification.classList.add('error');
    }

    // Auto-hide after 3 seconds
    setTimeout(() => {
        elements.notification.classList.add('hidden');
    }, 3000);
};

// Update the display of cards
const updateDisplay = () => {
    const currentPlayer = gameState.players[gameState.currentPlayerIndex];

    // Update player cards
    currentPlayer.cards.forEach((card, index) => {
        const cardElement = elements.playerCards[index];

        // Check if this card has been revealed
        if (currentPlayer.revealedCards.includes(index)) {
            renderCard(cardElement, card, true);
        } else {
            // Face down card
            cardElement.innerHTML = '?';
            cardElement.className = 'card';
        }

        // Add selected class if this card is selected
        if (index === gameState.selectedCardIndex) {
            cardElement.classList.add('selected');
        } else {
            cardElement.classList.remove('selected');
        }
    });

    // Update discard pile
    if (gameState.discardPile.length > 0) {
        renderCard(elements.discardCard, gameState.discardPile[gameState.discardPile.length - 1], true);
    } else {
        elements.discardCard.innerHTML = '';
        elements.discardCard.className = 'card';
    }

    // Update draw pile (always face down)
    if (gameState.drawPile.length > 0) {
        elements.drawCard.innerHTML = '?';
        elements.drawCard.className = 'card';
    } else {
        elements.drawCard.innerHTML = '';
        elements.drawCard.className = 'card';
    }

    // Update card counts
    elements.drawCount.textContent = gameState.drawPile.length;
    elements.discardCount.textContent = gameState.discardPile.length;
};

// Render a card with proper styling
const renderCard = (element, card, faceUp = false) => {
    if (!faceUp) {
        element.innerHTML = '?';
        element.className = 'card';
        return;
    }

    // Set card face up
    element.className = 'card face-up';

    // Set card color based on suit
    const isRed = card.suit === '♥' || card.suit === '♦';
    element.style.color = isRed ? '#e53935' : '#212121';

    // Create card content
    element.innerHTML = `
        <span class="card-rank">${card.rank}</span>
        <span class="card-suit top-left">${card.suit}</span>
        <span class="card-suit bottom-right">${card.suit}</span>
    `;
};

// Update the status bar
const updateStatusBar = () => {
    elements.playerNames.innerText = `Players: ${gameState.players.map(p => p.name).join(', ')}`;
    elements.turnIndicator.innerText = `Current Turn: ${gameState.players[gameState.currentPlayerIndex].name}`;

    if (gameState.knockedPlayer !== null) {
        elements.knockStatus.innerText = `${gameState.players[gameState.knockedPlayer].name} has knocked!`;
    } else {
        elements.knockStatus.innerText = '';
    }
};

// Calculate and update scores
const updateScores = () => {
    gameState.players.forEach((player, index) => {
        // Calculate score based on card values
        let score = 0;
        player.cards.forEach(card => {
            score += CARD_VALUES[card.rank];
        });

        // Update player score
        player.score = score;
        elements.playerScores[index].textContent = score;
    });
};

// End the game and show final scores
const endGame = () => {
    gameState.gameEnded = true;

    // Reveal all cards
    gameState.players.forEach(player => {
        player.revealedCards = [0, 1, 2, 3];
    });

    // Calculate final scores
    updateScores();

    // Determine winner
    let winnerIndex = 0;
    let lowestScore = gameState.players[0].score;

    for (let i = 1; i < gameState.players.length; i++) {
        if (gameState.players[i].score < lowestScore) {
            lowestScore = gameState.players[i].score;
            winnerIndex = i;
        }
    }

    // Show winner notification
    showNotification(`Game over! ${gameState.players[winnerIndex].name} wins with ${lowestScore} points!`);

    // Update display to show all cards
    updateDisplay();
};

// Event Handlers
// Draw a card from the draw pile
document.getElementById('draw-button').addEventListener('click', () => {
    if (gameState.gameEnded) {
        showNotification('The game has ended. Start a new game.', true);
        return;
    }

    if (gameState.drawPile.length === 0) {
        showNotification('No cards left in the draw pile!', true);
        return;
    }

    // Draw a card
    const drawnCard = gameState.drawPile.pop();
    gameState.potentialReplacement = drawnCard;

    // Show the drawn card
    showNotification(`You drew: ${drawnCard.rank}${drawnCard.suit}`);

    // Update display
    updateDisplay();
});

// Discard a card
document.getElementById('discard-button').addEventListener('click', () => {
    if (gameState.gameEnded) {
        showNotification('The game has ended. Start a new game.', true);
        return;
    }

    if (gameState.potentialReplacement === null) {
        showNotification('Draw a card first!', true);
        return;
    }

    // Discard the drawn card
    gameState.discardPile.push(gameState.potentialReplacement);
    gameState.potentialReplacement = null;

    // Switch turn
    gameState.currentPlayerIndex = (gameState.currentPlayerIndex + 1) % gameState.players.length;

    // Update display
    updateDisplay();
    updateStatusBar();

    // Check if game should end (after knock)
    if (gameState.knockedPlayer !== null && gameState.currentPlayerIndex === gameState.knockedPlayer) {
        endGame();
    }
});

// Handle the knock action
document.getElementById('knock-button').addEventListener('click', () => {
    if (gameState.gameEnded) {
        showNotification('The game has already ended.', true);
        return;
    }

    if (gameState.knockedPlayer !== null) {
        showNotification('Someone has already knocked!', true);
        return;
    }

    // Set knocked player
    gameState.knockedPlayer = gameState.currentPlayerIndex;
    showNotification(`${gameState.players[gameState.currentPlayerIndex].name} knocked! Each player gets one more turn.`);

    // Update status
    updateStatusBar();

    // Switch turn
    gameState.currentPlayerIndex = (gameState.currentPlayerIndex + 1) % gameState.players.length;
    updateStatusBar();
});

// Peek at a card
const peekCard = (cardIndex) => {
    if (gameState.gameEnded) {
        showNotification('The game has ended.', true);
        return;
    }

    const currentPlayer = gameState.players[gameState.currentPlayerIndex];
    const card = currentPlayer.cards[cardIndex];

    // Reveal the card if not already revealed
    if (!currentPlayer.revealedCards.includes(cardIndex)) {
        currentPlayer.revealedCards.push(cardIndex);
        showNotification(`You peeked at Card ${cardIndex + 1}: ${card.rank}${card.suit}`);
    }

    // Select this card for potential swap
    gameState.selectedCardIndex = cardIndex;

    // Update display
    updateDisplay();
};

// Add event listeners for each card to allow peeking
elements.playerCards.forEach((cardElement, index) => {
    cardElement.addEventListener('click', () => peekCard(index));
});

// Handle potential replacement from the discard pile
elements.discardCard.addEventListener('click', () => {
    if (gameState.gameEnded) {
        showNotification('The game has ended.', true);
        return;
    }

    if (gameState.discardPile.length === 0) {
        showNotification('No cards in the discard pile.', true);
        return;
    }

    // Get the top card from the discard pile
    const topCard = gameState.discardPile[gameState.discardPile.length - 1];
    gameState.potentialReplacement = topCard;

    // Remove the card from the discard pile
    gameState.discardPile.pop();

    showNotification(`You took ${topCard.rank}${topCard.suit} from the discard pile.`);

    // Update display
    updateDisplay();
});

// Swap action
document.getElementById('swap-button').addEventListener('click', () => {
    if (gameState.gameEnded) {
        showNotification('The game has ended.', true);
        return;
    }

    if (gameState.selectedCardIndex === null) {
        showNotification('Select a card to swap first.', true);
        return;
    }

    if (gameState.potentialReplacement === null) {
        showNotification('Draw a card or take from discard pile first.', true);
        return;
    }

    const currentPlayer = gameState.players[gameState.currentPlayerIndex];
    const replacedCard = currentPlayer.cards[gameState.selectedCardIndex];

    // Swap the cards
    currentPlayer.cards[gameState.selectedCardIndex] = gameState.potentialReplacement;

    // Add the replaced card to the discard pile
    gameState.discardPile.push(replacedCard);

    showNotification(`Swapped ${replacedCard.rank}${replacedCard.suit} with ${gameState.potentialReplacement.rank}${gameState.potentialReplacement.suit}`);

    // Clear selections
    gameState.selectedCardIndex = null;
    gameState.potentialReplacement = null;

    // Switch turn
    gameState.currentPlayerIndex = (gameState.currentPlayerIndex + 1) % gameState.players.length;

    // Update display
    updateDisplay();
    updateStatusBar();
    updateScores();

    // Check if game should end (after knock)
    if (gameState.knockedPlayer !== null && gameState.currentPlayerIndex === gameState.knockedPlayer) {
        endGame();
    }
});

// New game button
elements.newGameBtn.addEventListener('click', () => {
    initGame();
});

// Rules button
elements.rulesBtn.addEventListener('click', () => {
    elements.rulesModal.classList.add('show');
});

// Close modal button
elements.closeModalBtn.addEventListener('click', () => {
    elements.rulesModal.classList.remove('show');
});

// Close modal when clicking outside
window.addEventListener('click', (event) => {
    if (event.target === elements.rulesModal) {
        elements.rulesModal.classList.remove('show');
    }
});

// Theme toggle
elements.themeToggle.addEventListener('click', () => {
    document.body.classList.toggle('dark-theme');

    // Update icon
    const icon = elements.themeToggle.querySelector('i');
    if (document.body.classList.contains('dark-theme')) {
        icon.className = 'fas fa-sun';
    } else {
        icon.className = 'fas fa-moon';
    }
});

// Start the game
initGame();
