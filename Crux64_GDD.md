Crux64

Game Design Document Version: 1.0.1 Target Platform: Nintendo 64 (Homebrew) Engine/SDK: libdragon, Tiny3D, minimp3 Author: (c) 2026 Amanda Hariette-Scott and Cydonis Heavy Industries.
1. Overview
1.1 High-Level Concept
Crux64 is a physics-based, procedurally generated 3D mountain climbing simulation for the Nintendo 64. Inspired by the methodical, stamina-driven mechanics of Cairn, players control the individual limbs of a stick-figure climber to scale a treacherous, procedurally generated peak. Every movement requires calculation, balance, and stamina management to avoid plummeting to the bottom.
1.2 Core Pillars
    • Deliberate Mechanics: No automatic scaling. The player manually places hands and feet, reading the rock face and managing their center of gravity.
    • Procedural Yet Permanent: The mountain and skybox are generated using a fixed-seed procedural algorithm, ensuring a consistent, shared challenge for all players without storing massive map data in the ROM.
    • Visceral Feedback: Audio-visual and haptic cues (via the Rumble Pak) communicate physical exertion, grip strength, and impending failure.
    • Minimalist Aesthetic: A highly optimized 3D stick-figure player character and stylized low-poly environment maximize performance via Tiny3D.
1.3 Hardware Requirements
    • Expansion Pak (8MB RDRAM): Mandatory. Required for storing the generated heightmap arrays in memory, rendering the Tiny3D display lists, and allocating the necessary audio buffers for minimp3 playback.
    • Rumble Pak: Mandatory. Essential for communicating stamina loss and balance warnings to the player.
    • Standard N64 Controller: Required for limb-independent mapping.
2. Gameplay Mechanics
2.1 Physics & Posture System
The player's success hinges on the Center of Gravity (CoG) and the 3-Point Rule.
    • 3-Point Contact: The stick figure must maintain at least three solid points of contact (e.g., two feet, one hand) to remain completely stable.
    • Center of Gravity: If the CoG shifts too far from the base of support, the character's limbs will begin to shake, rapidly draining stamina.
    • Overextension: Reaching too far with a single limb exponentially increases stamina drain. Legs provide stronger lift than arms; players are encouraged to "push" up rather than "pull" up.
2.2 Stamina & Exertion
Stamina is tracked dynamically per limb and for the core body.
    • Visual Cues: The stick figure's limbs will violently shake when over-exerted.
    • Haptic Cues: The Rumble Pak provides progressive feedback. A light, pulsing rumble indicates rising fatigue. A heavy, sustained rumble means grip failure is imminent (1-2 seconds).
    • Recovery: Players can rest by finding a stable 3-point stance and pressing the R button to "shake out" the free limb, regaining stamina.
2.3 Tools & Pitons
    • Pitons: Players have a limited supply of pitons. Pressing Z drives a piton into the rock face.
    • Off-Belay: Once a piton is placed, the player can attach to it, fully restoring stamina and creating a respawn checkpoint in case of a fall.
2.4 Control Scheme
The N64 controller's unique layout is leveraged to map individual limbs intuitively:
    • C-Up: Select Right Arm
    • C-Left: Select Left Arm
    • C-Right: Select Right Leg
    • C-Down: Select Left Leg
    • Analog Stick: Move the selected limb in 3D space (XY plane relative to the wall). Releasing the C-Button snaps the limb to the nearest grip point.
    • Z Trigger: Place Piton / Clip-in (Off-Belay).
    • R Bumper: Shake out/Rest selected limb.
    • A Button: Apply Chalk (reduces stamina drain for the next 5 holds).
    • D-Pad: Move camera (Free-look to scout routes).
3. Technical Implementation
3.1 3D Graphics & Rendering (Tiny3D)
    • Engine & RSP Usage: Tiny3D is utilized to build and dispatch display lists to the RSP. To maintain a stable framerate (target 30fps), geometry is batched into manageable chunks, and backface culling is strictly enforced on the mountain mesh.
    • Character Model & IK: The stick figure is composed of a simplified skeletal hierarchy (torso, upper/lower arms, upper/lower legs). Tiny3D’s matrix stack is updated per-frame using a custom Inverse Kinematics (IK) solver to snap hands and feet to the procedurally generated grip points, while the torso sways dynamically based on Center of Gravity physics. Visual shaking from stamina loss is achieved by applying high-frequency rotational noise directly to the bone matrices.
    • Terrain Rendering & TMEM Limits: To bypass the N64's strict 4KB Texture Memory (TMEM) limits and keep memory free for the UI and skybox, the procedural mountain relies primarily on Gouraud shading via vertex colors calculated during generation. A single, highly compressed 32x32 detail texture is tiled over the mesh using Tiny3D's material system to provide tactile rock friction visuals.
    • Lighting & Atmosphere: A single directional light simulates the sun or moon, casting shading across the calculated normal map to highlight cracks and climbable routes. N64 hardware distance fog is heavily utilized—both to obscure the pop-in of generated terrain chunks and to simulate a dense, high-altitude atmospheric environment.
    • Dynamic Camera System: A collision-aware orbital camera ensures the player's viewport isn't clipped through the rock face. It dynamically zooms in on the character's core during delicate, high-stamina holds and smoothly pulls back during stable rests to showcase the sheer scale of the peak.
3.2 Procedural Generation (Fixed-Seed)
    • Heightmap Algorithm: The mountain is generated at boot using a layered Simplex noise algorithm seeded with a fixed constant (e.g., 0x63727578).
    • Mesh Generation: The heightmap is evaluated into a 3D vertex grid. Areas with steep gradients are designated as "cliff faces." Small high-frequency noise artifacts act as handholds/footholds.
    • Skybox: Generated via 2D Perlin noise applied to vertex colors on a large inverted sphere/cube, simulating shifting cloud covers or starry nights.
3.3 Audio Subsystem
    • Music (minimp3): Background music is highly compressed MP3 data stored in the ROM, decoded on the fly using minimp3 and fed to libdragon's audio mixer. Requires downsampling (e.g., 22kHz or 32kHz mono/stereo) to preserve CPU cycles for game logic.
    • Sound Effects (Procgen & DSP): To absolutely minimize ROM space and allow audio to dynamically react to gameplay, all SFX are procedurally synthesized in real-time. Custom DSP algorithms fill PCM buffers sent to the libdragon mixer, mapping audio directly to physics variables like velocity, altitude, and stamina.
        ◦ Wind (Environmental): A continuous white noise generator is passed through a dynamic low-pass filter. The filter's cutoff frequency and resonance are modulated by a slow Low-Frequency Oscillator (LFO). As the player gains altitude, the LFO amplitude increases, simulating fierce, howling high-altitude gusts.
        ◦ Climber Exertion (Voice): Procedural square and sawtooth wave pulses are shaped by ADSR envelopes to simulate human vocalizations. Pitch-bending and slight waveform clipping are applied proportionally to the player's stamina drain, creating dynamic grunts that sound increasingly stressed as the CoG fails.
        ◦ Heartbeat (Stamina UI): As stamina approaches zero, a low-frequency (40-60Hz) sine wave pulse begins. The BPM of this "heartbeat" accelerates synchronously with the Rumble Pak motor, creating a unified, panic-inducing audio-haptic warning state.
        ◦ Rock Interactions (Tactile Feedback):
            ▪ Hand/Foot Placement: Short bursts of filtered brown noise. Volume and frequency pitch are tied to the limb's velocity upon impact with the rock mesh.
            ▪ *Crumbling Hold (Warning): Randomized amplitude envelopes applied over low-pass noise simulate falling pebbles, acting as an auditory cue that a procedurally generated grip point is about to break.
            ▪ Piton Strike: A sharp, high-frequency metallic white-noise burst with a rapid exponential decay, layered with a low sine wave "thud".
3.4 Save System (EEPROM)
The game uses the standard N64 EEPROM via libdragon's eepromfs high-level/low-level API. Designed to fit within a minimal 4Kbit (512 bytes) or 16Kbit (2048 bytes) footprint.
EEPROM Data Structure (8-byte Blocks):
typedef struct {
    char initials[3];       // Player initials
    uint16_t max_altitude;  // Highest point reached (meters)
    uint16_t time_played;   // Total time in minutes
    uint8_t falls;          // Total falls
} save_data_t;
4. UI & HUD
In keeping with the survival/simulation aesthetic, the HUD is heavily minimized.
    • No Stamina Bar By Default: Stamina is communicated entirely via character animation (shaking), SFX (breathing/grunting/heartbeat), and Rumble Pak feedback. A toggle in the options menu enables it.
    • Inventory: A small icon in the bottom right corner showing the remaining number of Pitons and Chalk uses.
    • Altitude: Displayed subtly in the bottom left corner, updating only when the player successfully reaches a new rest point.
5. Development Roadmap
    1. Phase 1: Core N64 setup, libdragon environment config, Joypad Subsystem initialization (Rumble mapping).
    2. Phase 2: Implement fixed-seed Simplex noise and pipe output into a Tiny3D static mesh.
    3. Phase 3: Stick-figure IK (Inverse Kinematics) and limb-snapping logic.
    4. Phase 4: Posture, balance, and stamina math.
    5. Phase 5: Audio integration (minimp3 background loop + procgen wind/SFX via DSP algorithms).
    6. Phase 6: EEPROM save states, polish, and optimization.
Document Ends
