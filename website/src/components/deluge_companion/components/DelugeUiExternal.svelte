<script lang="ts">
  import { onDestroy } from "svelte";
  import { Action } from "../data/actions.js";
  import {
    getMenuContextDefinition,
    isVerticalMenuContext,
    normalizeMenuRenderMethod,
  } from "../data/menu_contexts.js";
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
  import { isStep, type Step, type StepOrSubstep } from "../types/shortcut.js";

  const qwertyPadColorMap = buildQwertyPadColorMap();
  const DISPLAY_RECT_FALLBACK = {
    x: 137.58,
    y: 34.396,
    width: 37.042,
    height: 11.245,
  };

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
  let activeMenuStep: Step | undefined;
  let isSvgLoaded = false;
  let loadStatus: string = "Initializing...";
  const MIN_VISIBLE_DEMO_INTENSITY = 0.035;
  const OLED_TEXT_CELL_WIDTH = 6;
  const OLED_TEXT_DRAW_HEIGHT = 7;
  const OLED_TEXT_TOP_OFFSET = 1;
  const OLED_TEXT_PIXEL_GAP_RATIO = 0.12;
  const OLED_MENU_HORIZONTAL_INSET_RATIO = 0.035;
  const OLED_MENU_VERTICAL_NUDGE_RATIO = 0.03;

  type FirmwareGlyph = {
    width: number;
    cols: readonly number[];
  };

  // Apple ][ 7px font data from firmware (font_apple/font_apple_desc), reduced to menu-relevant ASCII.
  const FIRMWARE_MENU_GLYPHS: Record<string, FirmwareGlyph> = {
    " ": { width: 2, cols: [0b00000000, 0b00000000] },
    "#": { width: 5, cols: [0b00010100, 0b01111111, 0b00010100, 0b01111111, 0b00010100] },
    "(": { width: 3, cols: [0b00011100, 0b00100010, 0b01000001] },
    ")": { width: 3, cols: [0b01000001, 0b00100010, 0b00011100] },
    "[": { width: 5, cols: [0b01111111, 0b01111111, 0b01000001, 0b01000001, 0b01000001] },
    "]": { width: 5, cols: [0b01000001, 0b01000001, 0b01000001, 0b01111111, 0b01111111] },
    "{": { width: 5, cols: [0b00001000, 0b00111110, 0b01110111, 0b01000001, 0b01000001] },
    "}": { width: 5, cols: [0b01000001, 0b01000001, 0b01110111, 0b00111110, 0b00001000] },
    "-": { width: 5, cols: [0b00001000, 0b00001000, 0b00001000, 0b00001000, 0b00001000] },
    "/": { width: 5, cols: [0b00100000, 0b00010000, 0b00001000, 0b00000100, 0b00000010] },
    ".": { width: 1, cols: [0b01000000] },
    ":": { width: 1, cols: [0b00010100] },
    "0": { width: 5, cols: [0b00111110, 0b01010001, 0b01001001, 0b01000101, 0b00111110] },
    "1": { width: 3, cols: [0b01000010, 0b01111111, 0b01000000] },
    "2": { width: 5, cols: [0b01100010, 0b01010001, 0b01001001, 0b01001001, 0b01000110] },
    "3": { width: 5, cols: [0b00100001, 0b01000001, 0b01001001, 0b01001101, 0b00110011] },
    "4": { width: 5, cols: [0b00011000, 0b00010100, 0b00010010, 0b01111111, 0b00010000] },
    "5": { width: 5, cols: [0b00100111, 0b01000101, 0b01000101, 0b01000101, 0b00111001] },
    "6": { width: 5, cols: [0b00111100, 0b01001010, 0b01001001, 0b01001001, 0b00110001] },
    "7": { width: 5, cols: [0b00000001, 0b01110001, 0b00001001, 0b00000101, 0b00000011] },
    "8": { width: 5, cols: [0b00110110, 0b01001001, 0b01001001, 0b01001001, 0b00110110] },
    "9": { width: 5, cols: [0b01000110, 0b01001001, 0b01001001, 0b00101001, 0b00011110] },
    "?": { width: 5, cols: [0b00000010, 0b00000001, 0b01011001, 0b00000101, 0b00000010] },
    A: { width: 5, cols: [0b01111100, 0b00010010, 0b00010001, 0b00010010, 0b01111100] },
    B: { width: 5, cols: [0b01111111, 0b01001001, 0b01001001, 0b01001001, 0b00110110] },
    C: { width: 5, cols: [0b00111110, 0b01000001, 0b01000001, 0b01000001, 0b00100010] },
    D: { width: 5, cols: [0b01111111, 0b01000001, 0b01000001, 0b01000001, 0b00111110] },
    E: { width: 5, cols: [0b01111111, 0b01001001, 0b01001001, 0b01001001, 0b01000001] },
    F: { width: 5, cols: [0b01111111, 0b00001001, 0b00001001, 0b00001001, 0b00000001] },
    G: { width: 5, cols: [0b00111110, 0b01000001, 0b01000001, 0b01010001, 0b01110001] },
    H: { width: 5, cols: [0b01111111, 0b00001000, 0b00001000, 0b00001000, 0b01111111] },
    I: { width: 3, cols: [0b01000001, 0b01111111, 0b01000001] },
    J: { width: 5, cols: [0b00100000, 0b01000000, 0b01000000, 0b01000000, 0b00111111] },
    K: { width: 5, cols: [0b01111111, 0b00001000, 0b00010100, 0b00100010, 0b01000001] },
    L: { width: 5, cols: [0b01111111, 0b01000000, 0b01000000, 0b01000000, 0b01000000] },
    M: { width: 5, cols: [0b01111111, 0b00000010, 0b00001100, 0b00000010, 0b01111111] },
    N: { width: 5, cols: [0b01111111, 0b00000100, 0b00001000, 0b00010000, 0b01111111] },
    O: { width: 5, cols: [0b00111110, 0b01000001, 0b01000001, 0b01000001, 0b00111110] },
    P: { width: 5, cols: [0b01111111, 0b00001001, 0b00001001, 0b00001001, 0b00000110] },
    Q: { width: 5, cols: [0b00111110, 0b01000001, 0b01010001, 0b00100001, 0b01011110] },
    R: { width: 5, cols: [0b01111111, 0b00001001, 0b00011001, 0b00101001, 0b01000110] },
    S: { width: 5, cols: [0b00100110, 0b01001001, 0b01001001, 0b01001001, 0b00110010] },
    T: { width: 5, cols: [0b00000001, 0b00000001, 0b01111111, 0b00000001, 0b00000001] },
    U: { width: 5, cols: [0b00111111, 0b01000000, 0b01000000, 0b01000000, 0b00111111] },
    V: { width: 5, cols: [0b00011111, 0b00100000, 0b01000000, 0b00100000, 0b00011111] },
    W: { width: 5, cols: [0b01111111, 0b00100000, 0b00011000, 0b00100000, 0b01111111] },
    X: { width: 5, cols: [0b01100011, 0b00010100, 0b00001000, 0b00010100, 0b01100011] },
    Y: { width: 5, cols: [0b00000011, 0b00000100, 0b01111000, 0b00000100, 0b00000011] },
    Z: { width: 5, cols: [0b01100001, 0b01010001, 0b01001001, 0b01000101, 0b01000011] },
  };

  function createSvgElement<K extends keyof SVGElementTagNameMap>(
    tagName: K,
  ): SVGElementTagNameMap[K] {
    return document.createElementNS("http://www.w3.org/2000/svg", tagName);
  }

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

  function clearMenuOverlay() {
    svgElement?.querySelectorAll(".dc-context-menu-overlay").forEach((el) => {
      el.remove();
    });
  }

  function clearDisplayFrameOverlay() {
    svgElement?.querySelectorAll(".dc-display-frame-overlay").forEach((el) => {
      el.remove();
    });
  }

  function getFirmwareGlyphForChar(char: string): FirmwareGlyph {
    const upper = char.toUpperCase();
    return FIRMWARE_MENU_GLYPHS[upper] ?? FIRMWARE_MENU_GLYPHS["?"];
  }

  function getDisplayAnchor() {
    if (!svgElement) {
      return {
        host: undefined,
        rect: DISPLAY_RECT_FALLBACK,
      };
    }

    const displayGroup = svgElement.querySelector("#OLED-Display");
    const displayRect = displayGroup?.querySelector("rect");

    if (!displayRect) {
      return {
        host: undefined,
        rect: DISPLAY_RECT_FALLBACK,
      };
    }

    const x = Number.parseFloat(displayRect.getAttribute("x") ?? "");
    const y = Number.parseFloat(displayRect.getAttribute("y") ?? "");
    const width = Number.parseFloat(displayRect.getAttribute("width") ?? "");
    const height = Number.parseFloat(displayRect.getAttribute("height") ?? "");

    if (
      !Number.isFinite(x) ||
      !Number.isFinite(y) ||
      !Number.isFinite(width) ||
      !Number.isFinite(height)
    ) {
      return {
        host: undefined,
        rect: DISPLAY_RECT_FALLBACK,
      };
    }

    return {
      host: displayGroup,
      rect: { x, y, width, height },
    };
  }

  function renderMenuOverlay() {
    if (!svgElement) {
      return;
    }

    const menuStep = activeMenuStep;
    if (!menuStep) {
      return;
    }

    const menu = getMenuContextDefinition(menuStep.menuContext);
    const menuTitle = menuStep.menuTitle ?? menu.title;
    const menuRenderMethod = normalizeMenuRenderMethod(
      menuStep.menuRenderMethod ?? menu.renderMethod,
    );
    const options =
      menuStep.menuOptions && menuStep.menuOptions.length > 0
        ? menuStep.menuOptions
        : menu.options.length > 0
          ? menu.options
          : ["..."];
    const displayAnchor = getDisplayAnchor();
    const displayRectRaw = displayAnchor.rect;
    const horizontalInset =
      displayRectRaw.width * OLED_MENU_HORIZONTAL_INSET_RATIO;
    const verticalNudge =
      displayRectRaw.height * OLED_MENU_VERTICAL_NUDGE_RATIO;
    const displayRect = {
      x: displayRectRaw.x + horizontalInset,
      y: displayRectRaw.y + verticalNudge,
      width: Math.max(displayRectRaw.width - horizontalInset * 2, 1),
      height: displayRectRaw.height,
    };
    const selectedIndex = Math.min(
      Math.max(menuStep.menuSelectedIndex ?? menu.selectedIndex ?? 0, 0),
      options.length - 1,
    );

    const isVerticalMenu = isVerticalMenuContext(menuStep.menuContext);
    const maxVisibleOptions = isVerticalMenu ? 3 : 2;
    let visibleOptions: string[] = [];
    let selectedVisibleIndex = 0;

    if (isVerticalMenu) {
      // Mirror Submenu::drawPixelsForOled windowing: collect relevant items before/after,
      // then balance the selected row toward the middle where possible.
      const before = options.slice(0, selectedIndex).slice(-maxVisibleOptions);
      const after = options.slice(selectedIndex, selectedIndex + maxVisibleOptions);

      let pos = Math.floor((maxVisibleOptions - 1) / 2);
      let tail = maxVisibleOptions - pos;

      if (before.length < pos) {
        pos = before.length;
        tail = Math.min(maxVisibleOptions - pos, after.length);
      } else if (after.length < tail) {
        tail = after.length;
        pos = Math.min(maxVisibleOptions - tail, before.length);
      }

      visibleOptions = [
        ...before.slice(before.length - pos),
        ...after.slice(0, tail),
      ];
      selectedVisibleIndex = pos;
    } else {
      visibleOptions = options.slice(0, maxVisibleOptions);
      selectedVisibleIndex = Math.min(selectedIndex, Math.max(visibleOptions.length - 1, 0));
    }
    // Mirror firmware context_menu.cpp virtual OLED geometry (128x48).
    const OLED_MAIN_WIDTH_PIXELS = 128;
    const OLED_MAIN_HEIGHT_PIXELS = 48;
    const K_TEXT_SPACING_X = OLED_TEXT_CELL_WIDTH;
    const K_TEXT_SPACING_Y = 9;
    const SCREEN_TITLE_Y = 1;
    const SCREEN_TITLE_SEPARATOR_Y = 12;
    const VERTICAL_BASE_Y = 14;
    const WINDOW_WIDTH = 100;
    const WINDOW_HEIGHT = 40;
    const windowMinX = (OLED_MAIN_WIDTH_PIXELS - WINDOW_WIDTH) / 2;
    const windowMaxX = OLED_MAIN_WIDTH_PIXELS - windowMinX;
    const windowMinY = (OLED_MAIN_HEIGHT_PIXELS - WINDOW_HEIGHT) / 2;
    const windowMaxY = OLED_MAIN_HEIGHT_PIXELS - windowMinY;

    const toDisplayX = (oledX: number) =>
      displayRect.x + (oledX / OLED_MAIN_WIDTH_PIXELS) * displayRect.width;
    const toDisplayY = (oledY: number) =>
      displayRect.y + (oledY / OLED_MAIN_HEIGHT_PIXELS) * displayRect.height;
    const pixelScaleX = displayRect.width / OLED_MAIN_WIDTH_PIXELS;
    const pixelScaleY = displayRect.height / OLED_MAIN_HEIGHT_PIXELS;

    const snapX = (value: number) =>
      displayRect.x +
      Math.round((value - displayRect.x) / Math.max(pixelScaleX, 0.0001)) *
        pixelScaleX;
    const snapY = (value: number) =>
      displayRect.y +
      Math.round((value - displayRect.y) / Math.max(pixelScaleY, 0.0001)) *
        pixelScaleY;
    const snapW = (value: number) =>
      Math.max(pixelScaleX, Math.round(value / Math.max(pixelScaleX, 0.0001)) * pixelScaleX);
    const snapH = (value: number) =>
      Math.max(pixelScaleY, Math.round(value / Math.max(pixelScaleY, 0.0001)) * pixelScaleY);

    const overlayGroup = createSvgElement("g");
    overlayGroup.setAttribute("class", "dc-context-menu-overlay");

    // Virtual OLED framebuffer: 1 bit per pixel, rendered into SVG rects.
    const oledBuffer = new Uint8Array(OLED_MAIN_WIDTH_PIXELS * OLED_MAIN_HEIGHT_PIXELS);
    const indexAt = (x: number, y: number) => y * OLED_MAIN_WIDTH_PIXELS + x;
    const setPixel = (x: number, y: number, value: 0 | 1) => {
      if (x < 0 || y < 0 || x >= OLED_MAIN_WIDTH_PIXELS || y >= OLED_MAIN_HEIGHT_PIXELS) {
        return;
      }
      oledBuffer[indexAt(x, y)] = value;
    };
    const invertPixel = (x: number, y: number) => {
      if (x < 0 || y < 0 || x >= OLED_MAIN_WIDTH_PIXELS || y >= OLED_MAIN_HEIGHT_PIXELS) {
        return;
      }
      const idx = indexAt(x, y);
      oledBuffer[idx] = oledBuffer[idx] ? 0 : 1;
    };

    const drawHorizontalLine = (y: number, startX: number, endX: number) => {
      for (let x = startX; x <= endX; x++) {
        setPixel(x, y, 1);
      }
    };

    const drawVerticalLine = (x: number, startY: number, endY: number) => {
      for (let y = startY; y <= endY; y++) {
        setPixel(x, y, 1);
      }
    };

    const drawRectangle = (minX: number, minY: number, maxX: number, maxY: number) => {
      drawHorizontalLine(minY, minX, maxX);
      drawHorizontalLine(maxY, minX, maxX);
      drawVerticalLine(minX, minY, maxY);
      drawVerticalLine(maxX, minY, maxY);
    };

    const drawMenuText = (text: string, startX: number, startY: number, endX: number) => {
      let pixelX = startX;
      const renderY = startY + OLED_TEXT_TOP_OFFSET;
      const uppercase = text.toUpperCase();

      for (let i = 0; i < uppercase.length; i++) {
        const glyph = getFirmwareGlyphForChar(uppercase[i] ?? " ");
        const glyphStartX = pixelX + ((OLED_TEXT_CELL_WIDTH - glyph.width) >> 1);

        for (let col = 0; col < glyph.width; col++) {
          const colBits = glyph.cols[col] ?? 0;
          for (let row = 0; row < OLED_TEXT_DRAW_HEIGHT; row++) {
            if (((colBits >> row) & 1) === 0) {
              continue;
            }
            setPixel(glyphStartX + col, renderY + row, 1);
          }
        }

        pixelX += OLED_TEXT_CELL_WIDTH;
        if (pixelX >= endX) {
          break;
        }
      }
    };

    const invertArea = (xMin: number, width: number, startY: number, endY: number) => {
      const xMax = xMin + width - 1;
      for (let y = startY; y <= endY; y++) {
        for (let x = xMin; x <= xMax; x++) {
          invertPixel(x, y);
        }
      }
    };

    const invertAreaRounded = (xMin: number, width: number, startY: number, endY: number) => {
      invertArea(xMin, width, startY, endY);
      // Match SMALL rounded-corner behavior (1px corner clear).
      const xMax = xMin + width - 1;
      setPixel(xMin, startY, 0);
      setPixel(xMax, startY, 0);
      setPixel(xMin, endY, 0);
      setPixel(xMax, endY, 0);
    };

    if (isVerticalMenu) {
      // Match MenuItem::renderOLED + Canvas::drawScreenTitle behavior for vertical submenus.
      drawMenuText(menuTitle, 0, SCREEN_TITLE_Y, OLED_MAIN_WIDTH_PIXELS);
      drawHorizontalLine(SCREEN_TITLE_SEPARATOR_Y, 0, OLED_MAIN_WIDTH_PIXELS - 1);

      // Match Submenu::drawSubmenuItemsForOled: rows at baseY + o*kTextSpacingY, full-width highlighting.
      visibleOptions.forEach((option, i) => {
        const textPixelY = VERTICAL_BASE_Y + i * K_TEXT_SPACING_Y;
        drawMenuText(option, K_TEXT_SPACING_X, textPixelY, OLED_MAIN_WIDTH_PIXELS);

        if (i === selectedVisibleIndex) {
          if (menuRenderMethod === "NO_INVERSION") {
            drawVerticalLine(0, textPixelY, textPixelY + 8);
          } else if (menuRenderMethod === "NO_ROUNDING") {
            invertArea(0, OLED_MAIN_WIDTH_PIXELS, textPixelY, textPixelY + 8);
          } else {
            invertAreaRounded(0, OLED_MAIN_WIDTH_PIXELS, textPixelY, textPixelY + 8);
          }
        }
      });
    } else {
      drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
      drawHorizontalLine(windowMinY + 15, 22, OLED_MAIN_WIDTH_PIXELS - 30);
      drawMenuText(menuTitle, 22, windowMinY + 6, OLED_MAIN_WIDTH_PIXELS);

      visibleOptions.forEach((option, i) => {
        const textPixelY = windowMinY + 18 + i * K_TEXT_SPACING_Y;
        const textStartX = menuRenderMethod === "NO_INVERSION" ? 23 + OLED_TEXT_CELL_WIDTH : 23;
        drawMenuText(option, textStartX, textPixelY, OLED_MAIN_WIDTH_PIXELS - 27);

        if (i === selectedVisibleIndex) {
          if (menuRenderMethod === "ROUNDED_INVERSION") {
            invertAreaRounded(22, OLED_MAIN_WIDTH_PIXELS - 44, textPixelY, textPixelY + 8);
          } else if (menuRenderMethod === "NO_INVERSION") {
            drawVerticalLine(22, textPixelY, textPixelY + 8);
          } else {
            invertArea(22, OLED_MAIN_WIDTH_PIXELS - 44, textPixelY, textPixelY + 8);
          }
        }
      });
    }

    const pixelInsetX = (pixelScaleX * OLED_TEXT_PIXEL_GAP_RATIO) / 2;
    const pixelInsetY = (pixelScaleY * OLED_TEXT_PIXEL_GAP_RATIO) / 2;
    const pixelWidth = pixelScaleX * (1 - OLED_TEXT_PIXEL_GAP_RATIO);
    const pixelHeight = pixelScaleY * (1 - OLED_TEXT_PIXEL_GAP_RATIO);

    for (let y = 0; y < OLED_MAIN_HEIGHT_PIXELS; y++) {
      for (let x = 0; x < OLED_MAIN_WIDTH_PIXELS; x++) {
        if (!oledBuffer[indexAt(x, y)]) {
          continue;
        }
        const px = createSvgElement("rect");
        px.setAttribute("class", "dc-context-menu-glyph-pixel");
        px.setAttribute("x", `${snapX(toDisplayX(x)) + pixelInsetX}`);
        px.setAttribute("y", `${snapY(toDisplayY(y)) + pixelInsetY}`);
        px.setAttribute("width", `${snapW(pixelWidth)}`);
        px.setAttribute("height", `${snapH(pixelHeight)}`);
        px.setAttribute("fill", "rgb(244, 247, 250)");
        overlayGroup.appendChild(px);
      }
    }

    if (displayAnchor.host) {
      displayAnchor.host.appendChild(overlayGroup);
    } else {
      svgElement.appendChild(overlayGroup);
    }
  }

  function renderDisplayFrameOverlay() {
    if (!svgElement) {
      return;
    }

    const displayAnchor = getDisplayAnchor();
    if (!displayAnchor.host) {
      return;
    }

    const { x, y, width, height } = displayAnchor.rect;
    const frameMaskPadding = 1.2;

    const overlayGroup = createSvgElement("g");
    overlayGroup.setAttribute("class", "dc-display-frame-overlay");

    // Mask the baked-in frame first, then redraw one uniform thin white border.
    const maskRect = createSvgElement("rect");
    maskRect.setAttribute("x", `${x - frameMaskPadding}`);
    maskRect.setAttribute("y", `${y - frameMaskPadding}`);
    maskRect.setAttribute("width", `${width + frameMaskPadding * 2}`);
    maskRect.setAttribute("height", `${height + frameMaskPadding * 2}`);
    maskRect.setAttribute("fill", "rgb(0,0,0)");
    maskRect.setAttribute("stroke", "none");
    overlayGroup.appendChild(maskRect);

    const frameRect = createSvgElement("rect");
    frameRect.setAttribute("x", `${x}`);
    frameRect.setAttribute("y", `${y}`);
    frameRect.setAttribute("width", `${width}`);
    frameRect.setAttribute("height", `${height}`);
    frameRect.setAttribute("fill", "none");
    frameRect.setAttribute("stroke", "rgb(231,232,233)");
    frameRect.setAttribute("stroke-opacity", "0.72");
    frameRect.setAttribute("stroke-width", "0.45");
    frameRect.setAttribute("shape-rendering", "geometricPrecision");
    overlayGroup.appendChild(frameRect);

    displayAnchor.host.appendChild(overlayGroup);
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

  function shouldUseStaticHighlight(actions: Set<Action> | undefined): boolean {
    return !!actions?.has(Action.HOLD) && !actions.has(Action.PRESS);
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

      activeMenuStep = activeControls.filter((step) => step.action === Action.MENU).at(-1);

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
          const actions = controlActions.get(control as Control);
          const shouldBlinkTurn = shouldBlinkTurnControl(actions);

          if (isTurnControl) {
            directSvgIds.forEach((id) => svgIdsToTurn.add(id));

            if (shouldBlinkTurn) {
              directSvgIds.forEach((id) => svgIdsToHighlight.add(id));
            } else {
              directSvgIds.forEach((id) => svgIdsToStaticHighlight.add(id));
            }
          } else if (shouldUseStaticHighlight(actions)) {
            directSvgIds.forEach((id) => svgIdsToStaticHighlight.add(id));
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
          const actions = controlActions.get(control as Control);
          const shouldBlinkTurn = shouldBlinkTurnControl(actions);

          if (isTurnControl) {
            svgIds.forEach((id) => svgIdsToTurn.add(id));
            if (shouldBlinkTurn) {
              svgIds.forEach((id) => svgIdsToHighlight.add(id));
            } else {
              svgIds.forEach((id) => svgIdsToStaticHighlight.add(id));
            }
          } else if (shouldUseStaticHighlight(actions)) {
            svgIds.forEach((id) => svgIdsToStaticHighlight.add(id));
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
    clearMenuOverlay();
    clearDisplayFrameOverlay();

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

    renderDisplayFrameOverlay();
    renderMenuOverlay();
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

  :global(.dc-context-menu-overlay) {
    pointer-events: none;
  }

  :global(.dc-display-frame-overlay) {
    pointer-events: none;
  }

  :global(.dc-context-menu-window) {
    fill: rgb(0, 0, 0);
    stroke: none;
    shape-rendering: crispEdges;
  }

  :global(.dc-context-menu-divider) {
    stroke: rgb(244, 247, 250);
    shape-rendering: crispEdges;
  }

  :global(.dc-context-menu-title) {
    fill: rgb(244, 247, 250);
    font-family: "Consolas", "Menlo", "Courier New", monospace;
    font-weight: 400;
    letter-spacing: 0;
    text-transform: uppercase;
    font-synthesis: none;
    font-kerning: none;
    font-variant-ligatures: none;
    text-rendering: geometricPrecision;
  }

  :global(.dc-context-menu-selection) {
    fill: rgb(244, 247, 250);
    shape-rendering: crispEdges;
  }

  :global(.dc-context-menu-option) {
    fill: rgb(244, 247, 250);
    font-family: "Consolas", "Menlo", "Courier New", monospace;
    font-weight: 400;
    letter-spacing: 0;
    text-transform: uppercase;
    font-synthesis: none;
    font-kerning: none;
    font-variant-ligatures: none;
    text-rendering: geometricPrecision;
  }

  :global(.dc-context-menu-option.is-selected) {
    fill: rgb(19, 23, 29);
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
