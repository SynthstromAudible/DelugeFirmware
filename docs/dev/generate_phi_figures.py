#!/usr/bin/env python3
"""Generate figures for phi triangle documentation."""

import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

# Output directory
OUTPUT_DIR = Path(__file__).parent / "images"
OUTPUT_DIR.mkdir(exist_ok=True)

# Golden ratio
PHI = 1.6180340

# Phi power constants matching the firmware
PHI_POWERS = {
    "φ^-0.5": PHI**-0.5,
    "φ^0.67": PHI**0.67,
    "φ^1.25": PHI**1.25,
    "φ^2.0": PHI**2.0,
}


def triangle_unipolar(phase, duty=1.0):
    """Unipolar triangle: 0→1→0 over duty portion, then 0."""
    phase = phase - np.floor(phase)
    half_duty = duty * 0.5

    result = np.zeros_like(phase)
    rising = phase < half_duty
    falling = (phase >= half_duty) & (phase < duty)

    result[rising] = phase[rising] / half_duty
    result[falling] = (duty - phase[falling]) / half_duty
    return result


def fig1_triangle_duty():
    """Figure 1: Triangle waveform with different duty cycles."""
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.5))

    phase = np.linspace(0, 2, 1000)

    # Full duty
    ax = axes[0]
    y = triangle_unipolar(phase, duty=1.0)
    ax.plot(phase, y, "b-", linewidth=2)
    ax.fill_between(phase, 0, y, alpha=0.3)
    ax.set_xlim(0, 2)
    ax.set_ylim(-0.1, 1.2)
    ax.set_xlabel("Phase (cycles)", fontsize=11)
    ax.set_ylabel("Output", fontsize=11)
    ax.set_title("duty = 1.0 (full triangle)", fontsize=12)
    ax.axhline(0, color="gray", linewidth=0.5)
    ax.grid(True, alpha=0.3)

    # Partial duty with deadzone
    ax = axes[1]
    y = triangle_unipolar(phase, duty=0.6)
    ax.plot(phase, y, "b-", linewidth=2)
    ax.fill_between(phase, 0, y, alpha=0.3)
    # Shade deadzone
    for i in range(2):
        ax.axvspan(
            i + 0.6,
            i + 1.0,
            alpha=0.15,
            color="red",
            label="deadzone" if i == 0 else None,
        )
    ax.set_xlim(0, 2)
    ax.set_ylim(-0.1, 1.2)
    ax.set_xlabel("Phase (cycles)", fontsize=11)
    ax.set_ylabel("Output", fontsize=11)
    ax.set_title("duty = 0.6 (with deadzone)", fontsize=12)
    ax.axhline(0, color="gray", linewidth=0.5)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper right")

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "fig1_triangle_duty.svg", bbox_inches="tight")
    plt.savefig(OUTPUT_DIR / "fig1_triangle_duty.png", dpi=150, bbox_inches="tight")
    plt.close()


def fig2_phi_frequencies():
    """Figure 2: Multiple parameters at different φ^n frequencies."""
    fig, ax = plt.subplots(figsize=(12, 5))

    position = np.linspace(0, 2.5, 1000)
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728"]

    for i, (name, freq) in enumerate(PHI_POWERS.items()):
        phase = position * freq
        y = triangle_unipolar(phase, duty=0.8)
        # Offset vertically for visibility
        offset = (3 - i) * 1.3
        ax.plot(
            position,
            y + offset,
            color=colors[i],
            linewidth=2,
            label=f"{name} = {freq:.3f}",
        )
        ax.fill_between(position, offset, y + offset, alpha=0.2, color=colors[i])
        ax.axhline(offset, color="gray", linewidth=0.3, linestyle="--")

    ax.set_xlim(0, 2.5)
    ax.set_ylim(-0.2, 5.5)
    ax.set_xlabel("Knob Position", fontsize=12)
    ax.set_ylabel("Parameter Values (stacked)", fontsize=12)
    ax.set_title(
        "Phi-Frequency Evolution: Each Parameter Cycles at Different Rate", fontsize=13
    )
    ax.legend(loc="upper right", fontsize=10)
    ax.set_yticks([])

    # Add position markers
    for x in [0.5, 1.0, 1.5, 2.0]:
        ax.axvline(x, color="gray", linewidth=0.5, alpha=0.5)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "fig2_phi_frequencies.svg", bbox_inches="tight")
    plt.savefig(OUTPUT_DIR / "fig2_phi_frequencies.png", dpi=150, bbox_inches="tight")
    plt.close()


def fig3_phase_offsets():
    """Figure 3: Phase offsets spreading parameters apart."""
    fig, axes = plt.subplots(2, 1, figsize=(10, 6))

    position = np.linspace(0, 1.5, 1000)
    freq = PHI**1.0  # Same frequency for all
    offsets = [0.00, 0.25, 0.50, 0.75]
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728"]
    labels = [
        "Param A (offset=0.00)",
        "Param B (offset=0.25)",
        "Param C (offset=0.50)",
        "Param D (offset=0.75)",
    ]

    # Top: stacked view
    ax = axes[0]
    for i, (offset, color, label) in enumerate(zip(offsets, colors, labels)):
        phase = position * freq + offset
        y = triangle_unipolar(phase, duty=0.8)
        stack_offset = (3 - i) * 1.2
        ax.plot(position, y + stack_offset, color=color, linewidth=2, label=label)
        ax.fill_between(
            position, stack_offset, y + stack_offset, alpha=0.2, color=color
        )

    ax.set_xlim(0, 1.5)
    ax.set_ylim(-0.2, 5)
    ax.set_ylabel("Stacked View", fontsize=11)
    ax.set_title(
        "Phase Offsets: Same Frequency, Different Starting Points", fontsize=13
    )
    ax.legend(loc="upper right", fontsize=9)
    ax.set_yticks([])

    # Bottom: overlaid view showing combinations
    ax = axes[1]
    for i, (offset, color) in enumerate(zip(offsets, colors)):
        phase = position * freq + offset
        y = triangle_unipolar(phase, duty=0.8)
        ax.plot(position, y, color=color, linewidth=1.5, alpha=0.8)
        ax.fill_between(position, 0, y, alpha=0.15, color=color)

    ax.set_xlim(0, 1.5)
    ax.set_ylim(-0.1, 1.2)
    ax.set_xlabel("Knob Position", fontsize=11)
    ax.set_ylabel("Overlaid View", fontsize=11)
    ax.set_title("Overlaid: Parameters Peak at Different Times", fontsize=12)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "fig3_phase_offsets.svg", bbox_inches="tight")
    plt.savefig(OUTPUT_DIR / "fig3_phase_offsets.png", dpi=150, bbox_inches="tight")
    plt.close()


def fig4_gamma_effect():
    """Figure 4: Gamma shifts the entire pattern."""
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.5))

    position = np.linspace(0, 1.0, 500)
    freq = PHI**1.25
    gamma_values = [0.0, 0.3, 0.7]
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]

    for ax, gamma in zip(axes, gamma_values):
        # gamma multiplied by 1024 and added to phase
        gamma_offset = gamma * 1024

        for i, (offset, color) in enumerate(zip([0.0, 0.25, 0.5], colors)):
            phase = position * freq + offset + gamma_offset
            y = triangle_unipolar(phase, duty=0.8)
            stack_offset = (2 - i) * 1.2
            ax.plot(position, y + stack_offset, color=color, linewidth=2)
            ax.fill_between(
                position, stack_offset, y + stack_offset, alpha=0.2, color=color
            )

        ax.set_xlim(0, 1.0)
        ax.set_ylim(-0.2, 4)
        ax.set_xlabel("Knob Position", fontsize=10)
        ax.set_title(f"γ = {gamma}", fontsize=12)
        ax.set_yticks([])

        # Mark a specific position
        ax.axvline(0.5, color="red", linewidth=1, linestyle="--", alpha=0.7)

    axes[0].set_ylabel("Parameters", fontsize=11)
    fig.suptitle(
        "Gamma Effect: Same Position, Different Pattern Regions", fontsize=13, y=1.02
    )

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "fig4_gamma_effect.svg", bbox_inches="tight")
    plt.savefig(OUTPUT_DIR / "fig4_gamma_effect.png", dpi=150, bbox_inches="tight")
    plt.close()


def fig5_combined_evolution():
    """Figure 5: Full evolution showing all concepts together."""
    fig, ax = plt.subplots(figsize=(14, 6))

    position = np.linspace(0, 1.0, 500)

    # Scatter timbral bank configuration
    configs = [
        ("reverseProb", PHI**-0.5, 0.50, 0.00, "#9467bd"),
        ("filterFreq", PHI**0.67, 0.70, 0.25, "#1f77b4"),
        ("delayFeed", PHI**1.25, 0.60, 0.50, "#2ca02c"),
        ("envShape", PHI**2.0, 0.80, 0.75, "#d62728"),
    ]

    for i, (name, freq, duty, offset, color) in enumerate(configs):
        phase = position * freq + offset
        y = triangle_unipolar(phase, duty=duty)
        stack_offset = (3 - i) * 1.3
        ax.plot(
            position,
            y + stack_offset,
            color=color,
            linewidth=2.5,
            label=f"{name}: φ^{np.log(freq) / np.log(PHI):.2f}, duty={duty}",
        )
        ax.fill_between(
            position, stack_offset, y + stack_offset, alpha=0.25, color=color
        )
        ax.axhline(stack_offset, color="gray", linewidth=0.3, linestyle="--")

        # Add parameter name on left
        ax.text(
            -0.02,
            stack_offset + 0.5,
            name,
            fontsize=10,
            ha="right",
            va="center",
            color=color,
        )

    ax.set_xlim(0, 1.0)
    ax.set_ylim(-0.3, 5.5)
    ax.set_xlabel("Zone Position (0 → 1)", fontsize=12)
    ax.set_title("Scatter Timbral Bank: Combined Phi Triangle Evolution", fontsize=14)
    ax.legend(loc="upper right", fontsize=9)
    ax.set_yticks([])

    # Add zone markers
    ax.axvline(0, color="black", linewidth=1)
    ax.axvline(1, color="black", linewidth=1)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "fig5_combined_evolution.svg", bbox_inches="tight")
    plt.savefig(
        OUTPUT_DIR / "fig5_combined_evolution.png", dpi=150, bbox_inches="tight"
    )
    plt.close()


def fig6_heatmap():
    """Figure 6: Heatmap showing parameter combinations over position."""
    fig, ax = plt.subplots(figsize=(12, 4))

    position = np.linspace(0, 1.0, 200)

    configs = [
        ("envShape", PHI**2.0, 0.80, 0.75),
        ("delayFeed", PHI**1.25, 0.60, 0.50),
        ("filterFreq", PHI**0.67, 0.70, 0.25),
        ("reverseProb", PHI**-0.5, 0.50, 0.00),
    ]

    data = np.zeros((len(configs), len(position)))
    for i, (name, freq, duty, offset) in enumerate(configs):
        phase = position * freq + offset
        data[i] = triangle_unipolar(phase, duty=duty)

    im = ax.imshow(
        data,
        aspect="auto",
        cmap="viridis",
        vmin=0,
        vmax=1,
        extent=[0, 1, -0.5, len(configs) - 0.5],
    )

    ax.set_yticks(range(len(configs)))
    ax.set_yticklabels([c[0] for c in configs], fontsize=11)
    ax.set_xlabel("Zone Position", fontsize=12)
    ax.set_title("Parameter Activation Heatmap: Phi Triangle Evolution", fontsize=13)

    cbar = plt.colorbar(im, ax=ax, shrink=0.8)
    cbar.set_label("Parameter Value", fontsize=10)

    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / "fig6_heatmap.svg", bbox_inches="tight")
    plt.savefig(OUTPUT_DIR / "fig6_heatmap.png", dpi=150, bbox_inches="tight")
    plt.close()


def main():
    print("Generating phi triangle figures...")

    fig1_triangle_duty()
    print("  ✓ fig1_triangle_duty")

    fig2_phi_frequencies()
    print("  ✓ fig2_phi_frequencies")

    fig3_phase_offsets()
    print("  ✓ fig3_phase_offsets")

    fig4_gamma_effect()
    print("  ✓ fig4_gamma_effect")

    fig5_combined_evolution()
    print("  ✓ fig5_combined_evolution")

    fig6_heatmap()
    print("  ✓ fig6_heatmap")

    print(f"\nAll figures saved to: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
