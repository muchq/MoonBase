// script.js
let playerCards = [];
let drawPile = [];
let discardPile = [];
let gameEnded = false; // Flag to check if the game has ended
let selectedCardIndex = null; // To track the selected card for swapping
let potentialReplacement = null; // To track the potential replacement card
let currentPlayerIndex = 0; // To track whose turn it is
const players = ["Player 1", "Player 2"]; // Example player names

// Initialize the game
const initGame = () => {
    drawPile = Array.from({ length: 52 }, (_, i) => (i % 13) + 1);
    shuffle(drawPile);

    playerCards = drawPile.splice(-4); // Draw 4 cards for the player
    updateDisplay();
    updateStatusBar(); // Update the status bar on game initialization
};

// Shuffle the deck
const shuffle = (array) => {
    for (let i = array.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [array[i], array[j]] = [array[j], array[i]];
    }
};

// Update the display of cards
const updateDisplay = () => {
    playerCards.forEach((card, index) => {
        document.getElementById(`card${index + 1}`).innerText = '?'; // Show card as face down
    });
    document.getElementById('discard-card').innerText = discardPile.length > 0 ? discardPile[discardPile.length - 1] : '';
    document.getElementById('draw-card').innerText = drawPile.length > 0 ? drawPile[drawPile.length - 1] : ''; // Show top card of draw pile
};

// Update the status bar
const updateStatusBar = () => {
    document.getElementById('player-names').innerText = `Players: ${players.join(', ')}`;
    document.getElementById('turn-indicator').innerText = `Current Turn: ${players[currentPlayerIndex]}`;
    document.getElementById('knock-status').innerText = ''; // Reset knock status
};

// Draw a card from the draw pile
document.getElementById('draw-button').addEventListener('click', () => {
    if (drawPile.length > 0 && !gameEnded) {
        const drawnCard = drawPile.pop();
        alert(`You drew a card: ${drawnCard}`);
        discardPile.push(drawnCard);
        updateDisplay();
        currentPlayerIndex = (currentPlayerIndex + 1) % players.length; // Switch turn
        updateStatusBar(); // Update status after the turn
    } else if (gameEnded) {
        alert('The game has ended. You cannot draw a card.');
    } else {
        alert('No cards left in the draw pile!');
    }
});

// Discard a card
document.getElementById('discard-button').addEventListener('click', () => {
    if (discardPile.length > 0 && !gameEnded) {
        const discardedCard = discardPile.pop();
        alert(`You discarded a card: ${discardedCard}`);
        updateDisplay();
        currentPlayerIndex = (currentPlayerIndex + 1) % players.length; // Switch turn
        updateStatusBar(); // Update status after the turn
    } else if (gameEnded) {
        alert('The game has ended. You cannot discard a card.');
    } else {
        alert('No cards to discard!');
    }
});

// Handle the knock action
document.getElementById('knock-button').addEventListener('click', () => {
    if (!gameEnded) {
        alert(`${players[currentPlayerIndex]} knocked! The round will end now.`);
        document.getElementById('knock-status').innerText = `${players[currentPlayerIndex]} has knocked!`;
        gameEnded = true; // Set the game to ended
    } else {
        alert('The game has already ended.');
    }
});

// Peek at a card
const peekCard = (cardIndex) => {
    if (gameEnded) {
        alert('The game has ended. You cannot peek at a card.');
        return;
    }

    const cardValue = playerCards[cardIndex];
    alert(`You peeked at Card ${cardIndex + 1}: ${cardValue}`);
    selectedCardIndex = cardIndex; // Set the selected card index
};

// Add event listeners for each card to allow peeking
for (let i = 0; i < 4; i++) {
    document.getElementById(`card${i + 1}`).addEventListener('click', () => peekCard(i));
}

// Handle potential replacement from the discard pile
const peekDiscardPile = () => {
    if (discardPile.length > 0) {
        const discardedCard = discardPile[discardPile.length - 1]; // Peek at the top card
        alert(`You peeked at the Discard Pile card: ${discardedCard}`);
        potentialReplacement = discardedCard; // Set potential replacement
    } else {
        alert('No cards in the discard pile to peek at.');
    }
};

// Handle potential replacement from the draw pile
const peekDrawPile = () => {
    if (drawPile.length > 0) {
        const drawnCard = drawPile[drawPile.length - 1]; // Peek at the top card
        alert(`You peeked at the Draw Pile card: ${drawnCard}`);
        potentialReplacement = drawnCard; // Set potential replacement
    } else {
        alert('No cards in the draw pile to peek at.');
    }
};

// Add event listeners for peeking at the discard and draw piles
document.getElementById('discard-card').addEventListener('click', peekDiscardPile);
document.getElementById('draw-button').addEventListener('click', peekDrawPile);

// Swap action
document.getElementById('swap-button').addEventListener('click', () => {
    if (selectedCardIndex === null || potentialReplacement === null) {
        alert('Please select a card to swap and peek at a potential replacement first.');
        return;
    }

    const replacedCard = playerCards[selectedCardIndex]; // Save the replaced card
    playerCards[selectedCardIndex] = potentialReplacement; // Swap the selected card with the potential replacement
    alert(`You swapped Card ${selectedCardIndex + 1} (${replacedCard}) with the selected card: ${potentialReplacement}`);

    // Clear selections
    selectedCardIndex = null;
    potentialReplacement = null;

    updateDisplay();
});

// Start the game
initGame();
