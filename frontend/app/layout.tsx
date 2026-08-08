import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "SKSS — Smart Key Storage System | IIT Mandi SAC",
  description:
    "Self-service key retrieval system for IIT Mandi Student Activity Center. Secure, audited, proximity-gated access.",
  keywords: ["key management", "IIT Mandi", "SAC", "access control"],
};

import SpotlightTracker from "@/components/SpotlightTracker";
import { ThemeProvider } from "@/components/ThemeProvider";

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="en">
      <head>
        <link rel="preconnect" href="https://fonts.googleapis.com" />
        <link
          rel="preconnect"
          href="https://fonts.gstatic.com"
          crossOrigin="anonymous"
        />
      </head>
      <body>
        <ThemeProvider>
          <SpotlightTracker />
          {children}
        </ThemeProvider>
      </body>
    </html>
  );
}
