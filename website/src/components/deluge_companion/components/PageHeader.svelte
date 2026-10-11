<!-- Companion page header with app-style branding. -->
<header class="dc-brand-header mt-8" aria-label="Deluge Companion">
  <h1 id="dc-companion-title" class="dc-brand-title mb-6" aria-label="delugeCompanion">
    <span class="dc-brand-word dc-brand-word-base">deluge</span
    ><span class="dc-brand-word dc-brand-word-accent">Companion</span>
  </h1>
</header>

<style>
  .dc-brand-header {
    display: block;
  }

  .dc-brand-title {
    display: inline-flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 0;
    margin-inline: 0;
    font-family: "Trebuchet MS", "Avenir Next", "Segoe UI", sans-serif;
    font-weight: 800;
    font-size: clamp(2.7rem, 7.1vw, 4.5rem);
    line-height: 0.92;
    letter-spacing: -0.03em;
  }

  .dc-brand-word {
    position: relative;
    display: inline-block;
    white-space: nowrap;
  }

  .dc-brand-word::after {
    content: "";
    position: absolute;
    left: 0;
    right: 0;
    bottom: -0.14em;
    border-bottom: 0.12em dashed #a855f7;
    opacity: 0.98;
  }

  .dc-brand-word-base {
    color: #161a22;
  }

  .dc-brand-word-accent {
    color: #a855f7;
    font-weight: 700;
  }

  :global(html[data-theme="dark"]) .dc-brand-word-base {
    color: var(--sl-color-white);
  }

  @media (max-width: 30rem) {
    .dc-brand-title {
      line-height: 0.98;
      row-gap: 0.14em;
    }
  }
</style>
