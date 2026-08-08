"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { proximity } from "@/lib/api";

interface ConnectPageClientProps {
  deviceId: string;
  code: string;
}

export default function ConnectPageClient({ deviceId, code }: ConnectPageClientProps) {
  const router = useRouter();
  const [status, setStatus] = useState<"verifying" | "success" | "error">("verifying");
  const [error, setError] = useState("");

  useEffect(() => {
    if (!deviceId || !code) {
      setStatus("error");
      setError("Missing device_id or code in URL. Are you connecting from the enclosure WiFi?");
      return;
    }

    async function verifyProximity() {
      try {
        const result = await proximity.verify(deviceId, code);
        if (result.proximity_verified) {
          setStatus("success");
          sessionStorage.setItem("proximity_verified", "true");
          sessionStorage.setItem("proximity_device_id", deviceId);
          setTimeout(() => router.push("/keys"), 1500);
        }
      } catch (err: any) {
        setStatus("error");
        setError(err.error || err.detail || "Proximity verification failed. The code may have expired.");
      }
    }

    verifyProximity();
  }, [deviceId, code, router]);

  return (
    <div className="auth-container">
      <div className="auth-card animate-fade-in" style={{ textAlign: "center" }}>
        {status === "verifying" && (
          <>
            <div style={{ fontSize: "3rem", marginBottom: "1rem" }}>📡</div>
            <h1 className="auth-title">Connecting to Enclosure</h1>
            <p className="auth-subtitle" style={{ marginTop: "0.5rem" }}>
              Verifying proximity code...
            </p>
            <div style={{ display: "flex", justifyContent: "center", marginTop: "2rem" }}>
              <div style={{ width: 40, height: 40, border: "3px solid var(--c-border)", borderTopColor: "var(--c-primary)", borderRadius: "50%", animation: "spin 0.8s linear infinite" }} />
            </div>
          </>
        )}

        {status === "success" && (
          <>
            <div style={{ fontSize: "3rem", marginBottom: "1rem", animation: "fadeIn 0.5s ease" }}>✅</div>
            <h1 className="auth-title" style={{ color: "var(--c-success)" }}>Proximity Verified!</h1>
            <p className="auth-subtitle" style={{ marginTop: "0.5rem" }}>
              You are at the enclosure. Redirecting to key management...
            </p>
            <div
              style={{
                marginTop: "1.5rem",
                padding: "0.75rem 1.25rem",
                background: "var(--c-success-glow)",
                border: "1px solid var(--c-success)",
                borderRadius: "var(--radius-sm)",
                fontSize: "0.85rem",
                color: "var(--c-success)",
              }}
            >
              ⏱️ Proximity flag valid for 5 minutes
            </div>
          </>
        )}

        {status === "error" && (
          <>
            <div style={{ fontSize: "3rem", marginBottom: "1rem" }}>⚠️</div>
            <h1 className="auth-title" style={{ color: "var(--c-warning)" }}>Verification Failed</h1>
            <p style={{ color: "var(--c-text-muted)", margin: "0.75rem 0 1.5rem", fontSize: "0.9rem" }}>{error}</p>
            <div className="proximity-banner">
              <span className="proximity-banner-icon">📶</span>
              <div className="proximity-banner-text">
                <h3>Connect to Enclosure WiFi</h3>
                <p>
                  Join the WiFi network at the SAC A-19 enclosure. The captive portal will
                  automatically redirect you to this page with a fresh code.
                </p>
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
}