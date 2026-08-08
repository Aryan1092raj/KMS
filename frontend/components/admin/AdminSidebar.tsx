"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

const navItems = [
  { href: "/admin", label: "Dashboard" },
  { href: "/admin/monitoring", label: "Live Monitoring" },
  { href: "/admin/users", label: "Users" },
  { href: "/admin/permissions", label: "Permissions" },
  { href: "/admin/rooms", label: "Rooms & Keys" },
  { href: "/admin/logs", label: "Audit Logs" },
  { href: "/admin/devices", label: "Device Health" },
  { href: "/admin/reports", label: "Reports" },
];

export default function AdminSidebar() {
  const pathname = usePathname();

  return (
    <aside className="sidebar">
      <div className="sidebar-logo">
        <div className="sidebar-logo-text">
          <span>SKSS</span> Admin
        </div>
      </div>

      <div className="nav-section">Navigation</div>

      {navItems.map(({ href, label }) => (
        <Link
          key={href}
          href={href}
          className={`nav-item${pathname === href ? " active" : ""}`}
          id={`nav-${label.toLowerCase().replace(/\s+/g, "-")}`}
        >
          {label}
        </Link>
      ))}

      <div style={{ flex: 1 }} />

      <div className="nav-section">Account</div>
      <Link href="/keys" className="nav-item">
        Key Portal
      </Link>
      <button
        className="nav-item"
        style={{ color: "var(--c-danger)" }}
        onClick={async () => {
          const { auth } = await import("@/lib/api");
          await auth.logout();
          window.location.href = "/login";
        }}
      >
        Sign Out
      </button>
    </aside>
  );
}
