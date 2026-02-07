const gameBoard = document.getElementById('game-board');
const movesDisplay = document.getElementById('moves');
const restartButton = document.getElementById('restart');

let cards = [];
let flippedCards = [];
let moves = 0;

const emojis = ['🐶', '🐱', '🐭', '🐹', '🐰', '🦊', '🐻', '🐼'];
const gameEmojis = [...emojis, ...emojis];

function shuffleArray(array) {
    for (let i = array.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [array[i], array[j]] = [array[j], array[i]];
    }
    return array;
}

function createCard(emoji) {
    const card = document.createElement('div');
    card.classList.add('card');
    card.dataset.emoji = emoji;
    card.addEventListener('click', flipCard);
    return card;
}

function flipCard() {
    if (flippedCards.length < 2 && !this.classList.contains('flipped')) {
        this.textContent = this.dataset.emoji;
        this.classList.add('flipped');
        flippedCards.push(this);

        if (flippedCards.length === 2) {
            moves++;
            movesDisplay.textContent = moves;
            setTimeout(checkMatch, 1000);
        }
    }
}

function checkMatch() {
    const [card1, card2] = flippedCards;
    if (card1.dataset.emoji === card2.dataset.emoji) {
        card1.removeEventListener('click', flipCard);
        card2.removeEventListener('click', flipCard);
    } else {
        card1.textContent = '';
        card2.textContent = '';
        card1.classList.remove('flipped');
        card2.classList.remove('flipped');
    }
    flippedCards = [];

    if (document.querySelectorAll('.flipped').length === gameEmojis.length) {
        alert(`Congratulations! You won in ${moves} moves!`);
    }
}

function initGame() {
    gameBoard.innerHTML = '';
    cards = [];
    flippedCards = [];
    moves = 0;
    movesDisplay.textContent = moves;

    const shuffledEmojis = shuffleArray(gameEmojis);
    shuffledEmojis.forEach(emoji => {
        const card = createCard(emoji);
        cards.push(card);
        gameBoard.appendChild(card);
    });
}

restartButton.addEventListener('click', initGame);

initGame();

// Chat logic
const chatSidebar = document.getElementById('chat-sidebar');
const chatMessages = document.getElementById('chat-messages');
const chatInput = document.getElementById('chat-input');
const sendButton = document.getElementById('send-button');
const toggleChatButton = document.getElementById('toggle-chat');

function addMessage(message) {
    const messageElement = document.createElement('p');
    messageElement.textContent = message;
    chatMessages.appendChild(messageElement);
    chatMessages.scrollTop = chatMessages.scrollHeight;
}

sendButton.addEventListener('click', () => {
    const message = chatInput.value.trim();
    if (message) {
        addMessage(`You: ${message}`);
        chatInput.value = '';
    }
});

chatInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') {
        sendButton.click();
    }
});

toggleChatButton.addEventListener('click', () => {
    chatSidebar.classList.toggle('open');
});
