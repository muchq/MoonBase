package com.muchq.mcpclient.oauth;

import java.awt.Desktop;
import java.io.IOException;
import java.net.URI;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Utility for opening the system web browser.
 *
 * Used to launch the OAuth authorization URL in the user's default browser.
 *
 * Fallback: If Desktop.browse() is not supported (headless systems),
 * the URL is printed to console for manual opening.
 */
public class BrowserLauncher {

    private static final Logger LOG = LoggerFactory.getLogger(BrowserLauncher.class);

    /**
     * Opens the specified URL in the system's default web browser.
     *
     * @param url The URL to open
     * @throws IOException if the browser cannot be launched
     */
    public static void open(String url) throws IOException {
        if (url == null || url.isEmpty()) {
            throw new IllegalArgumentException("URL is required");
        }

        try {
            if (Desktop.isDesktopSupported() && Desktop.getDesktop().isSupported(Desktop.Action.BROWSE)) {
                LOG.info("Opening browser: {}", url);
                Desktop.getDesktop().browse(URI.create(url));
            } else {
                // Fallback for headless systems
                LOG.warn("Desktop.browse() not supported. Please open this URL manually:");
                System.out.println("\n" + "=".repeat(80));
                System.out.println("Please open this URL in your browser to authorize:");
                System.out.println(url);
                System.out.println("=".repeat(80) + "\n");
            }
        } catch (Exception e) {
            LOG.error("Failed to open browser", e);

            // Fallback: print URL for manual opening
            System.err.println("\nFailed to open browser automatically.");
            System.err.println("Please open this URL manually:");
            System.err.println(url);
            System.err.println();

            throw new IOException("Failed to open browser: " + e.getMessage(), e);
        }
    }
}
