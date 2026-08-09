/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  // Emits .next/standalone/server.js — what the Dockerfile runner stage copies.
  output: "standalone",
  // Drops the `X-Powered-By: Next.js` response header.
  poweredByHeader: false,
  // Already the default; pinned because shipping maps hands over unminified source.
  productionBrowserSourceMaps: false,
  async headers() {
    return [
      {
        source: "/:path*",
        headers: [
          { key: "X-Content-Type-Options", value: "nosniff" },
          { key: "X-Frame-Options", value: "DENY" },
          { key: "Referrer-Policy", value: "no-referrer" },
          {
            key: "Permissions-Policy",
            value: "camera=(), microphone=(), geolocation=()",
          },
        ],
      },
    ];
  },
  async rewrites() {
    return [
      {
        source: "/api/:path*",
        // Server-side only. INTERNAL_API_URL is the compose-network address in
        // production; NEXT_PUBLIC_API_URL is what the browser bundle needs and is
        // deliberately not read here — it can't be, it's inlined at build time.
        destination: `${process.env.INTERNAL_API_URL || "http://localhost:8000"}/:path*`,
      },
    ];
  },
};

module.exports = nextConfig;
