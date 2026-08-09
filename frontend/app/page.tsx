"use client";

import Link from "next/link";
import { useEffect, useRef } from "react";
import { animate, stagger } from "animejs";
import Navbar from "@/components/Navbar";

const HERO_VIDEO =
  "https://designerstephen.github.io/public-assets/videos/serene-art-hero.mp4";

// Source: iitmandi.co.in — Student Gymkhana / Technical. Names verbatim; no
// invented descriptions, the source gives none beyond these expansions.
const CLUBS = [
  "Robotronics Club",
  "Space Technology and Astronomy Cell (STAC)",
  "Yantrik Club",
  "KamandPrompt — Programming Club",
  "Nirmaan Club",
  "SAE Collegiate",
  "Entrepreneurship Cell",
  "Kamand BioEngineering Group",
];

const EVENTS = ["Inter IIT Tech Meet", "Avishkar", "Utkarsh"];

/**
 * Fades a set of nodes in, staggered. The hidden state is applied here in JS,
 * not as `opacity: 0` in CSS: if this script never runs the content stays
 * visible instead of the page rendering blank. Costs one frame at full opacity
 * before the effect lands, which the 800ms fade absorbs.
 */
function riseIn(nodes: NodeListOf<Element>, reduced: boolean, delayStep: number) {
  nodes.forEach((n) => {
    (n as HTMLElement).style.opacity = "0";
  });
  return animate(nodes, {
    opacity: [0, 1],
    y: [24, 0],
    duration: reduced ? 0 : 800,
    delay: reduced ? 0 : stagger(delayStep),
    ease: "outQuad",
  });
}

export default function HomePage() {
  const heroRef = useRef<HTMLElement>(null);
  const councilRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const hero = heroRef.current;
    if (!hero) return;

    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    // Scoped to this hero's nodes, not every .hero-rise in the document.
    const entrance = riseIn(hero.querySelectorAll(".hero-rise"), reduced, 200);
    return () => {
      entrance.revert();
    };
  }, []);

  // Council cards wait until scrolled into view. IntersectionObserver is native —
  // no scroll library, no listener to throttle.
  useEffect(() => {
    const root = councilRef.current;
    if (!root) return;

    const cards = root.querySelectorAll(".reveal");
    if (!cards.length) return;

    const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    let running: ReturnType<typeof animate> | null = null;

    const io = new IntersectionObserver(
      (entries) => {
        if (!entries.some((e) => e.isIntersecting)) return;
        io.disconnect(); // fire once
        running = riseIn(cards, reduced, 60);
      },
      { threshold: 0.15 },
    );
    io.observe(root);

    return () => {
      io.disconnect();
      running?.revert();
    };
  }, []);

  return (
    <div className="home-shell">
      <Navbar />

      <main className="hero" ref={heroRef}>
        <video
          className="hero-video"
          src={HERO_VIDEO}
          autoPlay
          muted
          loop
          playsInline
          preload="metadata"
          aria-hidden="true"
          tabIndex={-1}
        />
        <div className="hero-scrim" />

        <div className="hero-inner">
          <p className="hero-eyebrow hero-rise">
            IIT Mandi · Science and Technology Council
          </p>
          <h1 className="hero-title hero-rise">
            Smart <em>Key</em> Storage System
          </h1>
          <p className="hero-lede hero-rise">
            Self-service key retrieval for SAC rooms, built and run by SnTC.
            Password, authenticator code, and physical presence at the enclosure —
            all three, every time.
          </p>
          <div className="hero-actions hero-rise">
            <Link href="/login" className="hero-btn hero-btn--solid">
              Sign In
            </Link>
            <Link href="/keys" className="hero-btn hero-btn--ghost">
              View Key Status
            </Link>
          </div>

          <dl className="hero-meta hero-rise">
            <div className="hero-meta-item">
              <dt>Clubs</dt>
              <dd>{CLUBS.length}</dd>
            </div>
            <div className="hero-meta-item">
              <dt>Gymkhana since</dt>
              <dd>2011</dd>
            </div>
            <div className="hero-meta-item">
              <dt>Flagship events</dt>
              <dd>{EVENTS.length}</dd>
            </div>
          </dl>
        </div>
      </main>

      <section className="council" ref={councilRef} aria-labelledby="council-heading">
        <div className="council-inner">
          <p className="council-eyebrow reveal">Student Gymkhana · Technical</p>
          <h2 className="council-heading reveal" id="council-heading">
            Science and Technology Council
          </h2>
          <p className="council-lede reveal">
            SnTC is IIT Mandi&apos;s technical society — a body of students advised
            by faculty. Its major events are the Inter IIT Tech Meet, Avishkar and
            Utkarsh.
          </p>

          <ul className="council-events reveal">
            {EVENTS.map((event) => (
              <li key={event} className="council-event">
                {event}
              </li>
            ))}
          </ul>

          <ul className="club-grid">
            {CLUBS.map((club) => (
              <li key={club} className="club-card reveal">
                {club}
              </li>
            ))}
          </ul>

          <p className="council-note reveal">
            Each club is coordinated by two coordinators and advised by a faculty
            advisor and a co-advisor. All clubs are open to every student at IIT
            Mandi, with a core team formed to run each one.
          </p>
          <p className="council-foot reveal">
            Student Gymkhana, IIT Mandi was established in 2011 and consists of
            eight secretaries organising student activities in their respective
            spheres.
          </p>
        </div>
      </section>
    </div>
  );
}
