# Repository artwork

## App artwork

117 uses an original bold numeral mark with cyan plasma on a dark navy background.
The same opaque artwork is used on mobile and the visionOS back layer. The
required visionOS front layer is fully transparent, keeping the visible image
together instead of separating it across depth planes. These are generated
project artwork, not extracted game textures, characters or official branding.
The generation prompt is retained in [117-plasma-icon.json](117-plasma-icon.json).

The launcher ships a native numeral **1** as its default game cover. Players
can use the artwork menu to choose their own image or restore the default.
Custom images are local preferences kept outside game data and saves, and
are not bundled or uploaded by this project. Neither app asset catalog ships
the previous helmet cover.

The exact reviewed icon paths and hashes are in `APP_ICON_ART` in
`tools/check_repository_hygiene.py`.

## Gameplay screenshots

The three `117-sense-*.png` stills in the main README come from the project
author's own Vision Pro gameplay recording on October 25, 2026. They show an
earlier development build, not proof of the latest build's behavior.

| File | Scene | Recording position |
| --- | --- | --- |
| `117-sense-rifle-combat.png` | Rifle combat with a visible hand and Sense controller | About 1:23.77 |
| `117-sense-sniper-scope.png` | Sniper scope and visible Sense controller | About 2:46.45 |
| `117-sense-grenade-throw.png` | Hand/controller during a throwing gesture | About 3:09.73 |

Frames were scaled from 1920 × 1080 to 1280 × 720 and saved as optimized RGB
PNGs with metadata removed. There is no AI generation, cropping or color edit.
The exact reviewed PNG paths and SHA-256 hashes are in `DOCUMENTATION_ART` in
`tools/check_repository_hygiene.py`.

These illustrative screenshots contain Halo: Combat Evolved game imagery.
Game-content rights remain with their respective owners. The screenshots are
separate from the MIT-licensed code and original app artwork; they are not
reusable game-data assets and imply no official endorsement.

## Historical upstream repository header

`master-chef-header.png` is generated documentation artwork, created with the
built-in GPT Image tool for this unofficial project. It is not extracted from
the game and is not an official Halo or Microsoft asset. Existing trademark
and non-affiliation notices apply. The original PNG includes generation
provenance. Dimensions: 2172 × 724 pixels.

Original generation prompt:

> Use case: logo-brand. Asset type: a polished GitHub repository README header, very wide landscape banner, approximately 3:1. Primary request: the classic Halo: Combat Evolved logo treatment, with the words "master chef" replacing Halo. Text verbatim: "master chef", spelled m-a-s-t-e-r, space, c-h-e-f. The ONLY text is master chef. Custom futuristic geometric Halo-style letterforms, brushed silver metallic faces with icy blue edges and a faint cyan glow; clean sharp typography, generous letter spacing, instantly readable. A thin sweeping elliptical Halo logo ring behind and around the wordmark. Deep midnight blue and black space backdrop with a subtle sparse star field and a restrained distant ringworld arc. Center the complete logo, comfortably within the image with wide margins so no letter or ring is cropped. Sophisticated restrained early-2000s science-fiction title art, faithful to Halo visual identity. Flat front-facing wordmark, no perspective distortion, no watermark, no subtitle, no extra logos, no characters or food.

The current header corrects the E and F in CHEF, including the final letter's
height and baseline. It was edited with the same built-in GPT Image tool,
using the original banner as its edit target. The original remains in the
v1.0.2 tag and release archive. Final edit prompt:

> Use case: text-localization.
> Asset type: existing GitHub README header banner.
> Input image: edit target, the existing master chef Halo-style banner.
> Primary request: correct ONLY the E and F in the word CHEF, the last two letters of the logo.
> Make the E in CHEF a clean, unmistakable E in the same futuristic Halo-style font as the E in MASTER: one strong vertical stem and three crisp horizontal bars. Make the final F an unmistakable F with one vertical stem and two horizontal bars, using the same letter design language. Critically, the F must match the E and H in cap height, baseline, thickness and visual size; the current F is too tall and raised. Give the corrected E and F balanced spacing and perfectly clean edges.
> Keep the exact text "master chef", spelled m-a-s-t-e-r space c-h-e-f. Preserve all other letters, their positions, metallic silver texture, bevels, icy blue glow, elliptical logo ring, stars, ringworld, lighting, background, composition, dimensions and wide margins. Change no content outside those two glyphs and their immediate glow. No new words, no added symbols, no watermark.
