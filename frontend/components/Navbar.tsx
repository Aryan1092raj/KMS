"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useEffect, useState } from "react";
import { useTheme } from "@/components/ThemeProvider";

export default function Navbar() {
  const pathname = usePathname();
  const { theme, toggleTheme } = useTheme();
  const [open, setOpen] = useState(false);

  useEffect(() => setOpen(false), [pathname]);

  return (
    <header className="navbar-wrapper">
      <div className="navbar-pill">
        <Link href="/" className="navbar-brand">
          <span>Robotronics</span>
        </Link>
        <button className="navbar-menu-toggle" type="button" onClick={() => setOpen(!open)} aria-expanded={open} aria-controls="site-navigation" aria-label={open ? "Close menu" : "Open menu"}>
          <span /><span /><span />
        </button>
        <nav className={`navbar-links${open ? " navbar-links--open" : ""}`} id="site-navigation">
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
