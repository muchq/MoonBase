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
            body: JSON.stringify({ longUrl: urlInput })
        });

        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }

        const data = await response.json();
        displayResult(data.slug);

    } catch (error) {
        displayError("Error shortening the URL. Please try again.");
        console.error(error);
    }
});

function displayResult(shortenedUrl) {
    const resultDiv = document.getElementById('result');
    resultDiv.textContent = `Shortened URL: https://r3dr.net/r/${shortenedUrl}`;
}

function displayError(message) {
    const errorDiv = document.getElementById('error');
    errorDiv.textContent = message;
}
