<script lang="ts">
  import {
    Control,
    controlDescriptions,
    ControlType,
  } from "../../data/targets";
  import CircleButton from "../../icons/CircleButton.svelte";
  import FullGrid from "../../icons/FullGrid.svelte";
  import Knob from "../../icons/Knob.svelte";
  import GridCol from "../../icons/GridCol.svelte";
  import Midi from "../../icons/Midi.svelte";
  import type { Step } from "../../types/shortcut";
  import { Action } from "../../data/actions";
  import horizontalIcon from "../../../../assets/icons/horizontal.svg?url";
  import verticalIcon from "../../../../assets/icons/vertical.svg?url";
  import selectIcon from "../../../../assets/icons/select.svg?url";
  import tempoIcon from "../../../../assets/icons/tempo.svg?url";
  import goldIcon from "../../../../assets/icons/gold.svg?url";
  import turnHorizontalIcon from "../../../../assets/icons/turn-horizontal.svg?url";
  import turnVerticalIcon from "../../../../assets/icons/turn-vertical.svg?url";
  import turnSelectIcon from "../../../../assets/icons/turn-select.svg?url";
  import turnTempoIcon from "../../../../assets/icons/turn-tempo.svg?url";
  import turnGoldIcon from "../../../../assets/icons/turn-gold.svg?url";
  import pressTurnHorizontalIcon from "../../../../assets/icons/press-turn-horizontal.svg?url";
  import pressTurnVerticalIcon from "../../../../assets/icons/press-turn-vertical.svg?url";
  import pressTurnSelectIcon from "../../../../assets/icons/press-turn-select.svg?url";
  import pressTurnTempoIcon from "../../../../assets/icons/press-turn-tempo.svg?url";
  import pressTurnGoldIcon from "../../../../assets/icons/press-turn-gold.svg?url";
  import keyboardButtonIcon from "../../../../assets/icons/button-keyboard.svg?url";

  export let step: Step;
  export let inline: boolean;
  export let compositeAction: "none" | "press-turn" = "none";

  // Lookup control metadata from shared target map.
  $: description = controlDescriptions[step.control];

  // Chooses image-based icon variants for press, turn, and press+turn combinations.
  $: controlIcon =
    compositeAction === "press-turn"
      ? step.control === Control.X
        ? { src: pressTurnHorizontalIcon, alt: "Press turn horizontal", scale: 1.24 }
        : step.control === Control.Y
          ? { src: pressTurnVerticalIcon, alt: "Press turn vertical", scale: 1.24 }
          : step.control === Control.SELECT
            ? { src: pressTurnSelectIcon, alt: "Press turn select", scale: 1.24 }
            : step.control === Control.TEMPO
              ? { src: pressTurnTempoIcon, alt: "Press turn tempo", scale: 1.24 }
              : step.control === Control.PARAMETER ||
                  step.control === Control.LOWER_PARAM ||
                  step.control === Control.UPPER_PARAM
                ? { src: pressTurnGoldIcon, alt: "Press turn gold", scale: 1.24 }
                : undefined
      : step.action === Action.TURN
        ? step.control === Control.X
          ? { src: turnHorizontalIcon, alt: "Turn horizontal", scale: 1.24 }
          : step.control === Control.Y
            ? { src: turnVerticalIcon, alt: "Turn vertical", scale: 1.24 }
            : step.control === Control.SELECT
              ? { src: turnSelectIcon, alt: "Turn select", scale: 1.24 }
              : step.control === Control.TEMPO
                ? { src: turnTempoIcon, alt: "Turn tempo", scale: 1.24 }
                : step.control === Control.PARAMETER ||
                    step.control === Control.LOWER_PARAM ||
                    step.control === Control.UPPER_PARAM
                  ? { src: turnGoldIcon, alt: "Turn gold", scale: 1.24 }
                  : undefined
        : step.control === Control.KEY || step.control === Control.PERFORMANCE
          ? { src: keyboardButtonIcon, alt: "Keyboard button", scale: 1.14 }
        : step.control === Control.X
          ? { src: horizontalIcon, alt: "Horizontal", scale: 1.24 }
          : step.control === Control.Y
            ? { src: verticalIcon, alt: "Vertical", scale: 1.24 }
            : step.control === Control.SELECT
              ? { src: selectIcon, alt: "Select", scale: 1.18 }
              : step.control === Control.TEMPO
                ? { src: tempoIcon, alt: "Tempo", scale: 1.18 }
                : step.control === Control.PARAMETER ||
                    step.control === Control.LOWER_PARAM ||
                    step.control === Control.UPPER_PARAM
                  ? { src: goldIcon, alt: "Gold knob", scale: 1.18 }
                  : undefined;
</script>

<!-- Branch: menu action uses only textual label. -->
{#if step.action === Action.MENU}
  <span class="target-icon" class:hidden={inline}>&nbsp;</span>
  <span class="target-title">{@html step.label}</span>
<!-- Branch: defensive fallback for invalid control types. -->
{:else if description.type === ControlType.none}
  <span class="target-icon font-bold text-[#f00]">INVALID</span>
<!-- Branch: image-backed controls (knobs and keyboard variants). -->
{:else if controlIcon}
  <span
    class="target-icon target-icon-control-image flex items-center justify-center"
    class:hidden={inline}
  >
    <img
      src={controlIcon.src}
      alt={controlIcon.alt}
      class="control-icon-image"
      style={`--dc-control-icon-scale: ${controlIcon.scale ?? 1};`}
    />
  </span>
  <span class="target-title">{@html step.label || description.title}</span>
<!-- Branch: generic circle button control. -->
{:else if description.type === ControlType.circleButton}
  <span
    class="target-icon target-icon-control-image text-[var(--sl-color-text)]"
    class:hidden={inline}
  ><CircleButton /></span>
  <span class="target-title uppercase">{@html description.title}</span>
<!-- Branch: grid pad control with lit/unlit coloring. -->
{:else if description.type === ControlType.grid}
  <span
    class={"target-icon " +
      (step.control === Control.GRID_LIT
        ? "text-[var(--sl-color-green-high)]"
        : "text-[var(--sl-color-gray-4)]")}
    class:hidden={inline}
  >
    <FullGrid />
  </span>
  <span class="target-title">{@html step.label || description.title}</span>
<!-- Branch: single grid-column icon control. -->
{:else if description.type === ControlType.gridCol}
  <span
    class={"target-icon " + (description.color && `text-${description.color}`)}
    class:hidden={inline}
  >
    <GridCol />
  </span>
  <span class="target-title">{@html description.title}</span>
<!-- Branch: black encoder icon control. -->
{:else if description.type === ControlType.blackKnob}
  <span class="target-icon text-[var(--sl-color-text)]" class:hidden={inline}
    ><Knob /></span
  >
  <span class="target-title uppercase">{@html description.title}</span>
<!-- Branch: gold encoder icon control. -->
{:else if description.type === ControlType.goldKnob}
  <span class="target-icon text-[var(--sl-color-accent)]" class:hidden={inline}
    ><Knob /></span
  >
  <span class="target-title uppercase">{@html description.title}</span>
<!-- Branch: display control renders monospace display token. -->
{:else if description.type === ControlType.display}
  <span class="target-icon" class:hidden={inline}>&nbsp;</span>
  <span class="target-title rounded bg-[var(--sl-color-black)]/85 px-2 font-mono text-[var(--sl-color-white)]"
    >{step.label}</span
  >
  <span class="target-title font-mono uppercase">{@html description.title}</span
  >
<!-- Branch: external MIDI control. -->
{:else if description.type === ControlType.external}
  <span class="target-icon text-[var(--sl-color-gray-4)]" class:hidden={inline}
    ><Midi /></span
  >
  <span class="target-title italic">{@html description.title}</span>
{/if}

<style lang="postcss">
  .target-icon {
    grid-area: target-icon;
    line-height: 1;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: var(--dc-step-icon-size, 1.35rem);
    height: var(--dc-step-icon-size, 1.35rem);
  }
  .target-title {
    text-align: center;
    font-size: var(--dc-step-font-size, 0.9rem);
    line-height: 1;
    text-transform: uppercase;
    white-space: nowrap;
    grid-area: target-title;
    align-self: baseline;
  }

  .target-icon :global(svg) {
    width: 100%;
    height: 100%;
  }

  .target-icon-control-image {
    width: var(--dc-step-icon-size, 1.35rem);
    height: var(--dc-step-icon-size, 1.35rem);
  }

  .control-icon-image {
    width: 100%;
    height: 100%;
    display: block;
    object-fit: contain;
    transform-origin: center;
    transform: scale(var(--dc-control-icon-scale, 1));
  }
</style>
