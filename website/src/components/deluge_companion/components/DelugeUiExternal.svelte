<script lang="ts">
  import { onDestroy } from "svelte";
  import { Action } from "../data/actions.js";
  import { Control } from "../data/targets.js";
  import { getControlCoordinates } from "../data/control_coordinates.js";
  import { getControlSvgIds } from "../data/control_svg_ids.js";
  import { hwCoordinates } from "../data/hardware_coordinates.js";
  import {
    appendTurnIndicator,
    buildQwertyPadColorMap,
    clearTurnIndicators,
    getLatestHorizontalZoomTurnAngle,
    pickActiveDelugeDemo,
    shouldBlinkTurnControl,
    type DemoCell,
    type DelugeDemoDefinition,
    type DelugeDemoLoop,
  } from "./demos/deluge_demos.js";
  import delugeSvgContent from "./svg/Deluge.svg?raw";
  import {
    buildCoordinateToSvgIdsMap,
    getSvgIdsForCoordinate,
  } from "./svg/svg_id_helper.js";
  import { isStep, type StepOrSubstep } from "../types/shortcut.js";

  const qwertyPadColorMap = buildQwertyPadColorMap();

  export let steps: StepOrSubstep[];

  // Build reverse lookup: coordinate string -> array of SVG element IDs.
  const coordinateToSvgIds = buildCoordinateToSvgIdsMap(hwCoordinates);

  let svgContainer: HTMLDivElement | undefined;
  let svgHost: HTMLDivElement | undefined;
  let svgElement: SVGSVGElement | undefined;
  let highlightedIds: Set<string> = new Set();
  let staticHighlightedIds: Set<string> = new Set();
  let turningIds: Set<string> = new Set();
  let qwertyColoredIds: Map<string, string> = new Map();
  let demoPadStyles: Map<string, { fill: string; stroke: string }> = new Map();
  let activeDemo: DelugeDemoDefinition | undefined;
  let activeDemoId: string | undefined;
  let activeDemoLoop: DelugeDemoLoop | undefined;
  let demoCells: DemoCell[] = [];
  let isSvgLoaded = false;
  let loadStatus: string = "Initializing...";
  const MIN_VISIBLE_DEMO_INTENSITY = 0.035;

  function extractCssDeclaration(style: string, property: string): string {
    const regex = new RegExp(`${property}\\s*:\\s*([^;]+)`, "i");
    const match = style.match(regex);
    return match?.[1]?.trim() ?? "";
  }

  function clearDemoOverlays() {
    svgElement?.querySelectorAll(".dc-demo-overlay").forEach((el) => {
      el.remove();
    });
  }

  function appendDemoOverlay(target: Element, fill: string, stroke: string) {
    if (!svgElement || !(target instanceof SVGGraphicsElement)) return;

    const ctm = svgElement.getScreenCTM();
    if (!ctm) return;

    const rect = target.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return;

    const topLeft = svgElement.createSVGPoint();
    topLeft.x = rect.left;
    topLeft.y = rect.top;
    const p0 = topLeft.matrixTransform(ctm.inverse());

    const bottomRight = svgElement.createSVGPoint();
    bottomRight.x = rect.right;
    bottomRight.y = rect.bottom;
    const p1 = bottomRight.matrixTransform(ctm.inverse());

    const inset = Math.min(p1.x - p0.x, p1.y - p0.y) * 0.085;
    const overlay = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    overlay.setAttribute("class", "dc-demo-overlay");
    overlay.setAttribute("x", `${p0.x + inset}`);
    overlay.setAttribute("y", `${p0.y + inset}`);
    overlay.setAttribute("width", `${Math.max(0, p1.x - p0.x - inset * 2)}`);
    overlay.setAttribute("height", `${Math.max(0, p1.y - p0.y - inset * 2)}`);
    overlay.setAttribute("rx", `${Math.max(0.6, inset * 0.9)}`);
    overlay.setAttribute("fill", fill || "rgba(178, 183, 190, 1)");
    overlay.setAttribute("stroke", stroke || "rgba(208, 213, 220, 1)");
    overlay.setAttribute("stroke-width", `${Math.max(0.35, inset * 0.42)}`);

    svgElement.appendChild(overlay);
  }

  // Load SVG when container becomes available
  $: if (svgHost && !isSvgLoaded) {
    console.log("[DelugeUiExternal] Container detected, loading SVG...");
    try {
      loadStatus = "Loading SVG...";
      console.log("[DelugeUiExternal] Loading inline SVG module");

      svgHost.innerHTML = delugeSvgContent;
      svgElement = svgHost.querySelector("svg") as SVGSVGElement;
      console.log("[DelugeUiExternal] SVG injected, element found?", !!svgElement);

      isSvgLoaded = true;
      loadStatus = "SVG loaded!";
      updateHighlights();
    } catch (error) {
      const msg = error instanceof Error ? error.message : String(error);
      console.error("[DelugeUiExternal] ERROR:", msg);
      loadStatus = `FAILED: ${msg}`;
    }
  }

  // Resolve active controls to SVG IDs (direct for non-grid, coordinates for grid).
  $: {
    if (isSvgLoaded) {
      const activeControls = steps
        .flatMap((step) => {
          if (isStep(step)) {
            return [step];
          } else {
            return step.substeps;
          }
        });

      const turnControls = new Set<Control>(
        activeControls
          .filter((step) => step.action === Action.TURN)
          .map((step) => step.control),
      );

      const controlActions = new Map<Control, Set<Action>>();
      for (const step of activeControls) {
        if (!controlActions.has(step.control)) {
          controlActions.set(step.control, new Set<Action>());
        }
        controlActions.get(step.control)?.add(step.action);
      }

      const activeControlIds = activeControls.map((step) => step.control);

      // Get all unique controls
      const uniqueControls = [...new Set(activeControlIds)];
      
      // Collect all SVG element IDs to highlight
      const svgIdsToHighlight = new Set<string>();
      const svgIdsToStaticHighlight = new Set<string>();
      const svgIdsToTurn = new Set<string>();
      const qwertyPadColors = new Map<string, string>();

      for (const control of uniqueControls) {
        if (control === Control.QWERTY) {
          for (const pad of qwertyPadColorMap) {
              const svgIds = getSvgIdsForCoordinate(coordinateToSvgIds, pad.x, pad.y);
            for (const id of svgIds) {
              qwertyPadColors.set(id, pad.color);
            }
          }
          continue;
        }

        // Non-grid controls resolve directly to SVG ids.
        const directSvgIds = getControlSvgIds(control as Control);
        if (directSvgIds.length > 0) {
          const isTurnControl = turnControls.has(control as Control);
          const shouldBlinkTurn = shouldBlinkTurnControl(controlActions.get(control as Control));

          if (isTurnControl) {
            directSvgIds.forEach((id) => svgIdsToTurn.add(id));

            if (shouldBlinkTurn) {
              directSvgIds.forEach((id) => svgIdsToHighlight.add(id));
            } else {
              directSvgIds.forEach((id) => svgIdsToStaticHighlight.add(id));
            }
          } else {
            directSvgIds.forEach((id) => svgIdsToHighlight.add(id));
          }
          continue;
        }

        // Grid-based controls continue to use coordinate mapping.
        const coords = getControlCoordinates(control);
        for (const coord of coords) {
          const svgIds = getSvgIdsForCoordinate(coordinateToSvgIds, coord.x, coord.y);
          const isTurnControl = turnControls.has(control as Control);
          const shouldBlinkTurn = shouldBlinkTurnControl(controlActions.get(control as Control));

          if (isTurnControl) {
            svgIds.forEach((id) => svgIdsToTurn.add(id));
            if (shouldBlinkTurn) {
              svgIds.forEach((id) => svgIdsToHighlight.add(id));
            } else {
              svgIds.forEach((id) => svgIdsToStaticHighlight.add(id));
            }
          } else {
            svgIds.forEach((id) => svgIdsToHighlight.add(id));
          }
        }
      }

      highlightedIds = svgIdsToHighlight;
      staticHighlightedIds = svgIdsToStaticHighlight;
      turningIds = svgIdsToTurn;
      qwertyColoredIds = qwertyPadColors;

      const nextDemoPadStyles = new Map<string, { fill: string; stroke: string }>();
      for (const cell of demoCells) {
        if (cell.intensity < MIN_VISIBLE_DEMO_INTENSITY) {
          continue;
        }

        const coordX = cell.col - 1;
        const coordY = 8 - cell.row;
        const svgIds = getSvgIdsForCoordinate(coordinateToSvgIds, coordX, coordY);
        const fillStyle = activeDemo?.getCellFillStyle(cell) ?? "";
        const fill = extractCssDeclaration(fillStyle, "fill");
        const stroke = extractCssDeclaration(fillStyle, "stroke");

        if (!fill && !stroke) {
          continue;
        }

        for (const id of svgIds) {
          nextDemoPadStyles.set(id, { fill, stroke });
        }
      }
      demoPadStyles = nextDemoPadStyles;

      updateHighlights();
    }
  }

  $: activeDemo = pickActiveDelugeDemo(steps);

  $: {
    const nextDemoId = activeDemo?.id;

    if (nextDemoId !== activeDemoId) {
      activeDemoLoop?.stop();
      activeDemoLoop = undefined;
      activeDemoId = undefined;
      demoCells = [];

      if (activeDemo) {
        activeDemoLoop = activeDemo.createLoop((cells) => {
          demoCells = cells;
          if (isSvgLoaded) {
            updateHighlights();
          }
        });
        activeDemoLoop.start();
        activeDemoId = activeDemo.id;
      }
    }
  }

  onDestroy(() => {
    activeDemoLoop?.stop();
  });

  const xControlSvgIds = new Set<string>(getControlSvgIds(Control.X));

  // Apply highlights to SVG elements
  function updateHighlights() {
    if (!svgContainer) return;

    // Remove all highlights first
    svgContainer.querySelectorAll(".dc-svg-highlight, .dc-svg-highlight-static, .dc-qwerty-pad, .dc-demo-cell").forEach((el) => {
      el.classList.remove("dc-svg-highlight");
      el.classList.remove("dc-svg-highlight-static");
      el.classList.remove("dc-qwerty-pad");
      el.classList.remove("dc-demo-cell");
      (el as SVGElement).style.removeProperty("--dc-qwerty-color");
      (el as SVGElement).style.removeProperty("--dc-demo-fill");
      (el as SVGElement).style.removeProperty("--dc-demo-stroke");
    });
    clearTurnIndicators(svgElement);
    clearDemoOverlays();

    // Add highlights to matched elements
    highlightedIds.forEach((id) => {
      const el = svgContainer?.querySelector(`[id="${id}"]`);
      if (el) {
        el.classList.add("dc-svg-highlight");
      }
    });

    // Add steady highlights for TURN-only controls (no PRESS action).
    staticHighlightedIds.forEach((id) => {
      const el = svgContainer?.querySelector(`[id="${id}"]`);
      if (el) {
        el.classList.add("dc-svg-highlight-static");
      }
    });

    // Add turning animation to controls driven by Action.TURN.
    turningIds.forEach((id) => {
      const el = svgContainer?.querySelector(`[id="${id}"]`);
      if (el) {
        const isHorizontalZoomDemo =
          activeDemoId === "horizontal-zoom" && xControlSvgIds.has(id);

        if (isHorizontalZoomDemo) {
          const turnAngle = getLatestHorizontalZoomTurnAngle() ?? 0;
          appendTurnIndicator(svgElement, el, { angleDeg: turnAngle });
        } else {
          appendTurnIndicator(svgElement, el);
        }
      }
    });

    // Apply QWERTY key coloring directly to main grid pads.
    qwertyColoredIds.forEach((color, id) => {
      const el = svgContainer?.querySelector(`[id="${id}"]`) as SVGElement | null;
      if (el) {
        el.classList.add("dc-qwerty-pad");
        el.style.setProperty("--dc-qwerty-color", color);
      }
    });

    // Apply demo-cell coloring for matching animation demos (e.g. horizontal zoom).
    demoPadStyles.forEach((style, id) => {
      const el = svgContainer?.querySelector(`[id="${id}"]`) as SVGElement | null;
      if (el) {
        appendDemoOverlay(el, style.fill, style.stroke);
      }
    });
  }
</script>

<div class="deluge-ui-external" bind:this={svgContainer}>
  <div class="dc-svg-host" bind:this={svgHost}></div>

  {#if !isSvgLoaded && loadStatus}
    <div style="padding: 1rem; background: rgba(255,100,100,0.1); border: 1px solid #ff6b6b; border-radius: 4px; font-family: monospace; font-size: 0.85rem; color: #ff6b6b;">
      {loadStatus}
    </div>
  {/if}
</div>

<style>
  .deluge-ui-external {
    position: relative;
    width: 100%;
    max-width: 100%;
  }

  :global(.deluge-ui-external svg) {
    width: 100%;
    height: auto;
    display: block;
  }

  :global(.dc-qwerty-pad circle),
  :global(.dc-qwerty-pad path),
  :global(.dc-qwerty-pad rect),
  :global(.dc-qwerty-pad ellipse),
  :global(.dc-qwerty-pad polygon) {
    fill: var(--dc-qwerty-color) !important;
    fill-opacity: 1 !important;
    opacity: 1 !important;
  }

  :global(.dc-qwerty-pad) {
    animation: dc-svg-blink 0.6s infinite !important;
    filter: none !important;
  }

  :global(.dc-svg-highlight) {
    animation: dc-svg-blink 0.6s infinite !important;
    filter: brightness(1.5) drop-shadow(0 0 8px rgba(34, 197, 94, 0.8)) !important;
  }

  :global(.dc-svg-highlight-static) {
    filter: brightness(1.5) drop-shadow(0 0 8px rgba(34, 197, 94, 0.8)) !important;
  }

  :global(.dc-demo-cell circle),
  :global(.dc-demo-cell path),
  :global(.dc-demo-cell rect),
  :global(.dc-demo-cell ellipse),
  :global(.dc-demo-cell polygon) {
    fill: var(--dc-demo-fill) !important;
    stroke: var(--dc-demo-stroke) !important;
    fill-opacity: 1 !important;
    opacity: 1 !important;
    filter: none !important;
    transition: fill 120ms linear, stroke 120ms linear !important;
  }

  :global(.dc-demo-overlay) {
    pointer-events: none;
    transition: fill 120ms linear, stroke 120ms linear;
  }

  :global(.dc-svg-highlight circle),
  :global(.dc-svg-highlight path),
  :global(.dc-svg-highlight rect),
  :global(.dc-svg-highlight ellipse),
  :global(.dc-svg-highlight-static circle),
  :global(.dc-svg-highlight-static path),
  :global(.dc-svg-highlight-static rect),
  :global(.dc-svg-highlight-static ellipse),
  :global(.dc-svg-highlight polygon) {
    fill: rgb(34, 197, 94) !important;
    fill-opacity: 1 !important;
    opacity: 1 !important;
    filter: drop-shadow(0 0 6px rgba(34, 197, 94, 0.9)) !important;
  }

  :global(.dc-turn-indicator) {
    stroke: rgb(36, 39, 43);
    stroke-width: 7;
    stroke-linecap: round;
    opacity: 0.98;
    filter: none;
    pointer-events: none;
  }

  @keyframes dc-svg-blink {
    0%,
    49% {
      opacity: 1;
    }
    50%,
    100% {
      opacity: 0.7;
    }
  }

  @media (prefers-reduced-motion: reduce) {
    :global(.dc-svg-highlight) {
      animation: none !important;
      opacity: 1 !important;
    }

    :global(.dc-qwerty-pad) {
      animation: none !important;
      opacity: 1 !important;
    }

  }
</style>
