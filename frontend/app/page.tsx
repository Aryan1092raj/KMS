"use client";

import Link from "next/link";
import { useEffect, useState } from "react";
import Navbar from "@/components/Navbar";

export default function HomePage() {
  const [mounted, setMounted] = useState(false);
  useEffect(() => setMounted(true), []);

  return (
    <div style={{ minHeight: "100vh", background: "var(--c-bg)", paddingBottom: "3rem" }}>
      <Navbar />
      <main className="auth-container" style={{ minHeight: "calc(100vh - 120px)", flexDirection: "column", alignItems: "center", textAlign: "center", gap: "2.5rem", paddingTop: "0" }}>
        <div style={{ maxWidth: 620 }} className="animate-fade-in">
          <div className="auth-logo">
            <span className="badge badge--admin" style={{ marginTop: "0.5rem", letterSpacing: "0.08em" }}>IIT MANDI SNTC</span>
          </div>
          <h1 style={{ fontSize: "clamp(2.4rem, 6vw, 3.8rem)", fontWeight: 800, marginBottom: "1rem", letterSpacing: "-0.03em", lineHeight: 1.15 }}>
            Smart Key Storage System
          </h1>
          <p style={{ fontSize: "1.15rem", color: "var(--c-text-muted)", marginBottom: "2.25rem", lineHeight: 1.7, maxWidth: 540, margin: "0 auto 2.25rem" }}>
            Secure, self-service key retrieval for SAC rooms.
            Authenticate with credentials + TOTP with zero wait time.
          </p>
          <div style={{ display: "flex", gap: "1rem", justifyContent: "center", flexWrap: "wrap" }}>
            <Link href="/login" className="btn btn-primary btn-lg">
              <span>Sign In</span>
            </Link>
            <Link href="/keys" className="btn btn-ghost btn-lg">
              <span>View Key Status</span>
            </Link>
          </div>
        </div>

        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fit, minmax(200px, 1fr))", gap: "1.25rem", maxWidth: 760, width: "100%" }}>
          {[
            { title: "Under 30s", desc: "From WiFi connect to key in hand" },
            { title: "2FA Secured", desc: "Password + TOTP authentication" },
            { title: "Full Audit", desc: "Every access event logged" },
          ].map(({ title, desc }) => (
            <div key={title} className="bezel-shell">
              <div className="bezel-core" style={{ textAlign: "center", padding: "1.5rem 1.25rem" }}>
                <div style={{ fontWeight: 700, fontSize: "1.05rem", marginBottom: "0.35rem" }}>{title}</div>
                <div style={{ fontSize: "0.82rem", color: "var(--c-text-muted)", lineHeight: 1.5 }}>{desc}</div>
              </div>
            </div>
          ))}
        </div>
      </main>
    </div>
  );
}
