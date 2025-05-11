// script.js

// --- WebSocket Communication ---
const WEBSOCKET_URL = 'ws://localhost:8080/ws/golf'; // Ensure this matches your Java server port
let socket = null;
let currentUserId = null;
let currentGameId = null; // Will be set when a game is joined/created
let recentlyDrawnOrPeekedCard = null; // Stores the card the current player just drew/peeked

// Message Type Constants (mirroring Java side)
const TYPE_REGISTER_USER = "REGISTER_USER";
const TYPE_NEW_GAME = "NEW_GAME";
const TYPE_JOIN_GAME = "JOIN_GAME";
const TYPE_PEEK = "PEEK";
const TYPE_DISCARD_DRAW = "DISCARD_DRAW";
const TYPE_SWAP_FOR_DRAW = "SWAP_FOR_DRAW";
const TYPE_SWAP_FOR_DISCARD = "SWAP_FOR_DISCARD";
const TYPE_KNOCK = "KNOCK";

const TYPE_USER_REGISTERED = "USER_REGISTERED";
const TYPE_GAME_STATE_UPDATE = "GAME_STATE_UPDATE";
const TYPE_ERROR = "ERROR";

// --- Original Card Data (can be kept for rendering if server sends abstract card data) ---
const SUITS = ['♠', '♥', '♦', '♣']; // Or map from server suit names
const RANKS = ['A', '2', '3', '4', '5', '6', '7', '8', '9', '10', 'J', 'Q', 'K']; // Or map from server rank names
// CARD_VALUES might not be needed if server handles all scoring

// --- Original Game State (largely replaced by server state) ---
// let gameState = { ... }; // This will be deprecated or minimal

// DOM Elements (existing)
const elements = {
    notification: document.getElementById('notification'),
    drawCount: document.getElementById('draw-count'),
    discardCount: document.getElementById('discard-count'),
    playerCardsElements: Array.from({ length: 4 }, (_, i) => document.getElementById(`card${i + 1}`)),
    drawCardElement: document.getElementById('draw-card'),
    discardCardElement: document.getElementById('discard-card'),
    playerNamesDisplay: document.getElementById('player-names'),
    turnIndicator: document.getElementById('turn-indicator'),
    knockStatus: document.getElementById('knock-status'),
    scoreDisplayContainer: document.getElementById('score-display'),
    themeToggle: document.getElementById('theme-toggle-btn'),
    rulesModal: document.getElementById('rules-modal'),
    closeModalBtn: document.querySelector('.close-button'),
    rulesBtn: document.getElementById('rules-button'),
    newGameBtn: document.getElementById('new-game-button'),
    // Action buttons
    drawButton: document.getElementById('draw-button'),
    swapButton: document.getElementById('swap-button'),
    discardButton: document.getElementById('discard-button'),
    knockButton: document.getElementById('knock-button'),
    // More UI elements for registration, new game, join game
    userIdInput: null, // To be created or use a prompt
    registerButton: null,
    createGameButton: null, // Can repurpose newGameBtn or add a new one
    joinGameButton: null,   // Need a new button for this
    gameIdInput: null       // For joining a game
};

function initializeUiElements() {
    // Create and append user ID input and register button if not in HTML
    if (!document.getElementById('user-id-section')) {
        const header = document.querySelector('.game-container header');
        const userIdSection = document.createElement('div');
        userIdSection.id = 'user-id-section';
        userIdSection.innerHTML = `
            <input type="text" id="user-id-input" placeholder="Enter User ID" />
            <button id="register-user-btn">Register</button>
            <button id="new-game-setup-btn" style="display:none;">New Game Setup</button>
            <input type="text" id="join-game-id-input" placeholder="Enter Game ID to Join" style="display:none;"/>
            <button id="join-game-btn" style="display:none;">Join Game</button>
        `;
        header.parentNode.insertBefore(userIdSection, header.nextSibling);

        elements.userIdInput = document.getElementById('user-id-input');
        elements.registerButton = document.getElementById('register-user-btn');
        elements.createGameButton = document.getElementById('new-game-setup-btn');
        elements.joinGameButton = document.getElementById('join-game-btn');
        elements.gameIdInput = document.getElementById('join-game-id-input');
    }
}

// Show notification (existing, might be slightly adapted)
const showNotification = (message, isError = false) => {
    elements.notification.textContent = message;
    elements.notification.classList.remove('hidden', 'error');
    if (isError) elements.notification.classList.add('error');
    setTimeout(() => elements.notification.classList.add('hidden'), isError ? 5000 : 3000);
};

// --- WebSocket Functions ---
function connectWebSocket() {
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
        showNotification('Already connected or connecting.', true);
        return;
    }
    if (!currentUserId) {
        showNotification('Please enter and register a User ID first.', true);
        return;
    }

    socket = new WebSocket(WEBSOCKET_URL);

    socket.onopen = () => {
        showNotification('Connected to server. Registering user...');
        sendMessageToServer(TYPE_REGISTER_USER, { userId: currentUserId });
    };

    socket.onmessage = (event) => {
        const serverMessage = JSON.parse(event.data);
        logger.log('Received from server:', serverMessage);

        switch (serverMessage.type) {
            case TYPE_USER_REGISTERED:
                showNotification(serverMessage.payload.message || 'User registered successfully!');
                // Enable New Game / Join Game UI
                elements.createGameButton.style.display = 'inline-block';
                elements.joinGameButton.style.display = 'inline-block';
                elements.gameIdInput.style.display = 'inline-block';
                elements.registerButton.disabled = true;
                elements.userIdInput.disabled = true;
                break;
            case TYPE_GAME_STATE_UPDATE:
                // TODO: Call function to update UI with serverMessage.payload (GameStatePayload)
                showNotification('Game state updated.');
                updateGameDisplay(serverMessage.payload);
                break;
            case TYPE_ERROR:
                showNotification(`Server Error: ${serverMessage.payload.message}`, true);
                if (serverMessage.payload.originalActionType === TYPE_REGISTER_USER) {
                    // Re-enable registration if it failed
                    currentUserId = null; // Clear stored user ID if registration failed
                    elements.registerButton.disabled = false;
                    elements.userIdInput.disabled = false;
                    elements.userIdInput.focus();
                }
                break;
            default:
                logger.log('Unknown message type from server:', serverMessage.type);
        }
    };

    socket.onerror = (error) => {
        logger.error('WebSocket Error:', error);
        showNotification('WebSocket connection error. Check console.', true);
        disableGameActions(); // Disable all actions if connection fails
    };

    socket.onclose = (event) => {
        showNotification(`Disconnected from server: ${event.reason || 'Connection closed'}`);
        socket = null;
        disableGameActions();
        // Optionally re-enable registration UI
        elements.registerButton.disabled = false;
        elements.userIdInput.disabled = false;
        elements.createGameButton.style.display = 'none';
        elements.joinGameButton.style.display = 'none';
        elements.gameIdInput.style.display = 'none';
    };
}

function sendMessageToServer(type, payload) {
    if (socket && socket.readyState === WebSocket.OPEN) {
        const message = { type, payload };
        socket.send(JSON.stringify(message));
        logger.log('Sent to server:', message);
    } else {
        showNotification('Not connected to server. Cannot send message.', true);
    }
}

// --- UI Update Functions (to be refactored for server data) ---
function updateGameDisplay(gameStatePayload) {
    if (!gameStatePayload) {
        logger.error('Cannot update display: GameStatePayload is null');
        return;
    }
    currentGameId = gameStatePayload.gameId;
    logger.log('Updating display for game:', currentGameId, 'Version:', gameStatePayload.version);

    // Update player names in status bar
    elements.playerNamesDisplay.textContent = `Players: ${gameStatePayload.players.join(', ')}`;

    // Dynamically update player scores
    elements.scoreDisplayContainer.innerHTML = ''; // Clear previous scores
    gameStatePayload.players.forEach((playerId, index) => {
        const playerScoreDiv = document.createElement('div');
        playerScoreDiv.className = 'player-score';
        const score = gameStatePayload.scores[index] !== undefined ? gameStatePayload.scores[index] : '-';
        playerScoreDiv.innerHTML = `<span>${playerId}: </span><span id="player-${playerId}-score">${score}</span>`;
        elements.scoreDisplayContainer.appendChild(playerScoreDiv);
    });

    elements.turnIndicator.textContent = `Current Turn: ${gameStatePayload.currentTurnPlayerId}`;
    elements.knockStatus.textContent = gameStatePayload.knocker ? `${gameStatePayload.knocker} has knocked!` : '';

    // Update pile counts
    elements.drawCount.textContent = gameStatePayload.drawSize;
    elements.discardCount.textContent = gameStatePayload.discardSize;
    renderCard(elements.discardCardElement, gameStatePayload.topDiscard, !!gameStatePayload.topDiscard);

    // Handle the card peeked/drawn from draw pile
    if (gameStatePayload.yourTurn && gameStatePayload.topDraw) {
        recentlyDrawnOrPeekedCard = gameStatePayload.topDraw;
        renderCard(elements.drawCardElement, recentlyDrawnOrPeekedCard, true);
        elements.drawButton.textContent = 'Take Draw Pile Card'; // Change button text after peeking
                                                              // Or rename drawButton to peekButton and have a separate takeButton
    } else {
        recentlyDrawnOrPeekedCard = null;
        renderCard(elements.drawCardElement, null, false); // Show as '?' if not your turn or no topDraw
        elements.drawButton.textContent = 'Peek/Draw'; // Reset button text
    }

    // Update player's hand (only for current player, based on payload.hand)
    const hand = gameStatePayload.hand;
    elements.playerCardsElements.forEach((cardEl, index) => {
        let cardData = null;
        if (hand) { // hand is only sent for the current player if relevant
            if (index === 0) cardData = hand.topLeft;
            else if (index === 1) cardData = hand.topRight;
            else if (index === 2) cardData = hand.bottomLeft;
            else if (index === 3) cardData = hand.bottomRight;
        }
        renderCard(cardEl, cardData, !!cardData);
    });

    // Enable/disable action buttons based on whose turn it is
    const myTurn = gameStatePayload.yourTurn;
    elements.drawButton.disabled = !myTurn || gameStatePayload.gameOver || !!recentlyDrawnOrPeekedCard; // Disable if card already drawn/peeked
    elements.discardButton.disabled = !myTurn || gameStatePayload.gameOver || !recentlyDrawnOrPeekedCard; // Enable only if card drawn/peeked
    elements.swapButton.disabled = !myTurn || gameStatePayload.gameOver || !recentlyDrawnOrPeekedCard; // Enable only if card drawn/peeked (and hand card selected)
    elements.knockButton.disabled = !myTurn || gameStatePayload.gameOver || !!gameStatePayload.knocker;

    if (gameStatePayload.gameOver) {
        showNotification(`Game Over! Winner details need to be processed from final scores.`);
    }
}

function renderCard(element, cardData, faceUp) {
    if (!element) return;
    if (!faceUp || !cardData) {
        element.innerHTML = '?';
        element.className = 'card';
        element.style.color = ''; // Reset color
        return;
    }
    element.className = 'card face-up';
    const isRed = cardData.suit === 'Hearts' || cardData.suit === 'Diamonds'; // Match Java enum names
    element.style.color = isRed ? '#e53935' : '#212121';
    element.innerHTML = ''; // Clear existing content
    const rankSpan = document.createElement('span');
    rankSpan.className = 'card-rank';
    rankSpan.textContent = cardData.rank.replace('Ten', '10').replace('Jack', 'J').replace('Queen', 'Q').replace('King', 'K').replace('Ace', 'A').substring(0, 2);
    const suitTopLeftSpan = document.createElement('span');
    suitTopLeftSpan.className = 'card-suit top-left';
    suitTopLeftSpan.textContent = SUITS_MAP[cardData.suit] || '?';
    const suitBottomRightSpan = document.createElement('span');
    suitBottomRightSpan.className = 'card-suit bottom-right';
    suitBottomRightSpan.textContent = SUITS_MAP[cardData.suit] || '?';
    element.appendChild(rankSpan);
    element.appendChild(suitTopLeftSpan);
    element.appendChild(suitBottomRightSpan);
}

const SUITS_MAP = { 'Clubs': '♣', 'Diamonds': '♦', 'Hearts': '♥', 'Spades': '♠' };

// --- Action Button Event Listeners (to be refactored) ---
function setupActionButtons() {
    elements.registerButton.addEventListener('click', () => {
        const userIdVal = elements.userIdInput.value.trim();
        if (userIdVal) {
            currentUserId = userIdVal;
            connectWebSocket();
        } else {
            showNotification('Please enter a User ID.', true);
        }
    });

    elements.createGameButton.addEventListener('click', () => {
        const numPlayers = parseInt(prompt('Enter number of players (2-N):', '2'));
        if (currentUserId && numPlayers && numPlayers >= 2) {
            sendMessageToServer(TYPE_NEW_GAME, { userId: currentUserId, numberOfPlayers: numPlayers });
        } else {
            showNotification('Invalid number of players or not registered.', true);
        }
    });

    elements.joinGameButton.addEventListener('click', () => {
        const gameIdToJoin = elements.gameIdInput.value.trim();
        if (currentUserId && gameIdToJoin) {
            sendMessageToServer(TYPE_JOIN_GAME, { userId: currentUserId, gameId: gameIdToJoin });
        } else {
            showNotification('Enter a Game ID to join or not registered.', true);
        }
    });

    elements.drawButton.addEventListener('click', () => { // This button now triggers PEEK
        if (!currentUserId || !currentGameId) return showNotification('Not in a game or not registered.', true);
        if (recentlyDrawnOrPeekedCard) { // If a card is already shown, this button might mean "Take it"
            // This logic needs to be expanded: what does clicking Draw again mean?
            // For now, let's assume first click is PEEK. If card shown, then other buttons (Discard, Swap) are used.
            showNotification('You have already peeked/drawn. Choose to Discard or Swap.', true);
            return;
        }
        sendMessageToServer(TYPE_PEEK, { userId: currentUserId, gameId: currentGameId });
    });

    elements.discardButton.addEventListener('click', () => {
        if (!currentUserId || !currentGameId) return showNotification('Not in a game or not registered.', true);
        if (!recentlyDrawnOrPeekedCard) return showNotification('No card has been drawn/peeked to discard.', true);

        sendMessageToServer(TYPE_DISCARD_DRAW, { userId: currentUserId, gameId: currentGameId });
        // Server needs to know it's the *drawn* card being discarded.
        // The gRPC DiscardDrawRequest has no fields for which card, implies it's the one from Peek/Draw.
        recentlyDrawnOrPeekedCard = null; // Clear after action
    });

    elements.swapButton.addEventListener('click', () => {
        if (!currentUserId || !currentGameId) return showNotification('Not in a game or not registered.', true);
        if (!recentlyDrawnOrPeekedCard) return showNotification('No card drawn/peeked to swap with.', true);

        // TODO: Implement hand card selection logic
        const selectedHandCardPosition = getSelectedHandCardPosition(); // This function needs to be created
        if (!selectedHandCardPosition) {
            showNotification('Please select one of your hand cards to swap with.', true);
            return;
        }
        sendMessageToServer(TYPE_SWAP_FOR_DRAW, {
            userId: currentUserId,
            gameId: currentGameId,
            position: selectedHandCardPosition
        });
        recentlyDrawnOrPeekedCard = null; // Clear after action
        clearSelectedHandCard(); // Clear selection after attempting swap
    });

    elements.knockButton.addEventListener('click', () => {
        if (!currentUserId || !currentGameId) return showNotification('Not in a game or not registered.', true);
        sendMessageToServer(TYPE_KNOCK, { userId: currentUserId, gameId: currentGameId });
    });

    // Add click listeners for player hand cards (for selection during swap)
    elements.playerCardsElements.forEach((cardEl, index) => {
        cardEl.addEventListener('click', () => {
            handlePlayerCardClick(index);
        });
    });
}

let selectedHandCardIndex = null; // Stores index 0-3 of player's hand card selected for swap

function handlePlayerCardClick(index) {
    if (!currentUserId || !currentGameId || !socket || socket.readyState !== WebSocket.OPEN) {
        showNotification('Not connected or not in a game.', true);
        return;
    }
    // Retrieve current game state from a global variable if stored, or request if needed
    // For now, assume gameStatePayload is somewhat accessible or we check conditions directly
    // This part needs robust access to the current client-side understanding of the game state.
    // Let's assume we need to check if it is our turn and if a card is drawn.
    // This check was: if (!gameStatePayload || !gameStatePayload.yourTurn || recentlyDrawnOrPeekedCard == null)
    // This check should ideally use a locally stored, up-to-date gameState from the last server message.

    // Let's make a placeholder for the client-side game state object that is updated by updateGameDisplay
    // window.clientGameState = {}; // And updateGameDisplay would set this.
    // Then check: if (!clientGameState.yourTurn || !recentlyDrawnOrPeekedCard) ...

    if (elements.drawButton.disabled && elements.discardButton.disabled && elements.swapButton.disabled && !recentlyDrawnOrPeekedCard) {
         // A simple check: if action buttons that require a drawn card are disabled, and no card is held,
         // it might not be the phase to select a card for swap.
         // This is a temporary guard. A better way is to check against a faithfully updated local game state copy.
         logger.log('Cannot select hand card now - not your turn or no card drawn to swap with.');
         showNotification('Cannot select hand card now.', true);
         return;
    }

    // Remove 'selected' class from previously selected card
    if (selectedHandCardIndex !== null && elements.playerCardsElements[selectedHandCardIndex]) {
        elements.playerCardsElements[selectedHandCardIndex].classList.remove('selected');
    }

    // If clicking the already selected card, deselect it
    if (selectedHandCardIndex === index) {
        selectedHandCardIndex = null;
        showNotification('Card selection cleared.');
    } else {
        selectedHandCardIndex = index;
        elements.playerCardsElements[index].classList.add('selected');
        showNotification(`Card at position ${index} selected for swap.`);
    }
}

function clearSelectedHandCard() {
    if (selectedHandCardIndex !== null && elements.playerCardsElements[selectedHandCardIndex]) {
        elements.playerCardsElements[selectedHandCardIndex].classList.remove('selected');
    }
    selectedHandCardIndex = null;
}

function getSelectedHandCardPosition() {
    if (selectedHandCardIndex === null) return null;
    // Convert index (0-3) to Position string (TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT)
    // This must match the enum values expected by the server/proto
    switch (selectedHandCardIndex) {
        case 0: return 'TOP_LEFT';
        case 1: return 'TOP_RIGHT';
        case 2: return 'BOTTOM_LEFT';
        case 3: return 'BOTTOM_RIGHT';
        default: return null;
    }
}

function disableGameActions(disableAll = true) {
    elements.drawButton.disabled = true;
    elements.swapButton.disabled = true;
    elements.discardButton.disabled = true;
    elements.knockButton.disabled = true;
    if (elements.createGameButton) elements.createGameButton.style.display = 'none';
    if (elements.joinGameButton) elements.joinGameButton.style.display = 'none';
    if (elements.gameIdInput) elements.gameIdInput.style.display = 'none';
    // Registration button should be handled by its own logic based on connection status
}

// --- Initialization ---
// Simplified logger
const logger = {
    log: (...args) => console.log('[GolfClient]', ...args),
    error: (...args) => console.error('[GolfClient]', ...args)
};

document.addEventListener('DOMContentLoaded', () => {
    initializeUiElements();
    disableGameActions();
    setupActionButtons();
    showNotification('Please enter a User ID and Register to connect.');

    // Theme toggle and rules modal (can remain as is)
    elements.themeToggle.addEventListener('click', () => {
        document.body.classList.toggle('dark-theme');
        const icon = elements.themeToggle.querySelector('i');
        icon.className = document.body.classList.contains('dark-theme') ? 'fas fa-sun' : 'fas fa-moon';
    });
    elements.rulesBtn.addEventListener('click', () => elements.rulesModal.classList.add('show'));
    elements.closeModalBtn.addEventListener('click', () => elements.rulesModal.classList.remove('show'));
    window.addEventListener('click', (event) => {
        if (event.target === elements.rulesModal) elements.rulesModal.classList.remove('show');
    });
});

// Player card click (peek) logic will also need to be adapted to send server messages.
// elements.playerCardsElements.forEach((cardElement, index) => { ... old logic ... });
