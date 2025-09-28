document.getElementById('submitBtn').addEventListener('click', async () => {
    const urlInput = document.getElementById('urlInput').value.trim();

    if (!urlInput) {
        displayError("Please enter a valid URL.");
        return;
    }

    const expiry = (+new Date) + 7 * 24 * 60 * 60 * 1000;

    try {
        const response = await fetch('/shorten', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ longUrl: urlInput, expiresAt: expiry })
        });

        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }

        const data = await response.json();
        clearError();
        displayResult(data.slug);
        document.getElementById('urlInput').value = "";

    } catch (error) {
        displayError("Error shortening the URL. Please try again.");
        clearResults();
        console.error(error);
    }
});

function displayResult(shortenedUrl) {
    const resultDiv = document.getElementById('result');
    const fullUrl = `https://r3dr.net/r/${shortenedUrl}`;

    resultDiv.innerHTML = `
        <div class="result-content">
            <div class="result-url">${fullUrl}</div>
            <button class="copy-btn" onclick="copyToClipboard('${fullUrl}', this)">Copy</button>
        </div>
    `;
}

function clearResults() {
    const resultDiv = document.getElementById('result');
    resultDiv.innerHTML = "";
}

function displayError(message) {
    const errorDiv = document.getElementById('error');
    errorDiv.textContent = message;
}

function clearError() {
    const errorDiv = document.getElementById('error');
    errorDiv.textContent = "";
}

async function copyToClipboard(text, button) {
    try {
        await navigator.clipboard.writeText(text);

        // Update button state
        const originalText = button.textContent;
        button.textContent = 'Copied!';
        button.classList.add('copied');

        // Reset button after 2 seconds
        setTimeout(() => {
            button.textContent = originalText;
            button.classList.remove('copied');
        }, 2000);

    } catch (err) {
        // Fallback for browsers that don't support clipboard API
        const textArea = document.createElement('textarea');
        textArea.value = text;
        textArea.style.position = 'fixed';
        textArea.style.left = '-999999px';
        textArea.style.top = '-999999px';
        document.body.appendChild(textArea);
        textArea.focus();
        textArea.select();

        try {
            document.execCommand('copy');
            button.textContent = 'Copied!';
            button.classList.add('copied');

            setTimeout(() => {
                button.textContent = 'Copy';
                button.classList.remove('copied');
            }, 2000);
        } catch (fallbackErr) {
            button.textContent = 'Failed';
            setTimeout(() => {
                button.textContent = 'Copy';
            }, 2000);
        }

        document.body.removeChild(textArea);
    }
}

