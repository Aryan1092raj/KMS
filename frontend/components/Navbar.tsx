"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useTheme } from "@/components/ThemeProvider";

export default function Navbar() {
  const pathname = usePathname();
  const { theme, toggleTheme } = useTheme();

  return (
    <header className="navbar-wrapper">
      <div className="navbar-pill">
        <Link href="/" className="navbar-brand">
          <span>Robotronics</span>
        </Link>
        <nav className="navbar-links">
          <Link
            href="/"
            className={`navbar-link${pathname === "/" ? " active" : ""}`}
          >
            Home
          </Link>
          <Link
            href="/keys"
            className={`navbar-link${pathname === "/keys" ? " active" : ""}`}
          >
            Key Status
          </Link>
          <Link
            href="/login"
            className={`navbar-link${pathname === "/login" || pathname?.startsWith("/login") ? " active" : ""}`}
          >
            Sign In
          </Link>
          <button
            onClick={toggleTheme}
            className="theme-toggle-btn"
            aria-label="Toggle Theme"
          >
            {theme === "dark" ? "Light Mode" : "Dark Mode"}
          </button>
        </nav>
      </div>
    </header>
  );
}
