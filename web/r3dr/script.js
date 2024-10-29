document.getElementById('submitBtn').addEventListener('click', async () => {
    const urlInput = document.getElementById('urlInput').value.trim();

    if (!urlInput) {
        displayError("Please enter a valid URL.");
        return;
    }

    try {
        const response = await fetch('/shorten', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                longUrl: urlInput,
                expiresAt: Date.now() + 1000 * 60 * 60 * 24 * 30,
            })
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
    resultDiv.textContent = `Shortened URL: https://r3dr.net/r/${shortenedUrl}`;
}

function clearResults() {
    const resultDiv = document.getElementById('result');
    resultDiv.textContent = "";
}

function displayError(message) {
    const errorDiv = document.getElementById('error');
    errorDiv.textContent = message;
}

function clearError() {
    const errorDiv = document.getElementById('error');
    errorDiv.textContent = "";
}
