#pragma once

#include "CoreMinimal.h"

/** How sprites are layered (report ch. 04 / 06). */
enum class EPixelSortMode : uint8
{
	Off,            // plain per-pixel depth: geometry interpenetrates
	CharactersOnly, // improvement 3 subset: flatten characters to their foot pivot depth
	Full,           // full precedence graph, ranks written into depth
	Count
};

/** What to do with a strongly connected component of the precedence graph. */
enum class EPixelCycleMode : uint8
{
	MinArea,    // the talk: cut the edge with the smallest screen overlap
	Merge,      // improvement 1: non-intersecting members keep true depth inside one layer
	Hysteresis, // improvement 2: min area, but keep last frame's cut while it stays within 1.2x
	Count
};

enum class EPixelOutlineMode : uint8
{
	Off,
	Front, // the talk: the line belongs to the nearer object
	Back,
	Both,
	Count
};

enum class EPixelDitherMode : uint8
{
	Off,
	Screen, // Bayer pattern fixed to the screen: swims on moving objects
	Object, // Bayer pattern anchored to the object pivot: stable once snapped
	Count
};

/** How the camera follows the player (the source of most perceived judder in pixel-art 3D). */
enum class EPixelFollow : uint8
{
	Layered,      // smooth sub-texel camera for the world, the followed character composited unshifted: both steady
	Locked,       // camera rides the player's snapped position: player still on screen, world steps with it
	SmoothSub,    // camera follows the continuous position, sub-texel shift: smooth world, player wobbles 0.5 texel
	Lagged,       // eased follow without sub-texel: two unsynchronised quantisations beat against each other
	Count
};

enum class EPixelViewMode : uint8
{
	Final,
	ObjectId,
	SortLevel,
	TrueDepth,
	PushedDepth,
	LightOnly,
	Count
};

/** Every switch the demo exposes. One instance lives in UPixelArtSubsystem. */
struct FPixelSettings
{
	// Resolution & upscale
	bool bLowRes = true;
	int32 PixelScale = 4;          // device pixels per texel
	bool bBilinear = false;
	bool bShowGrid = false;
	float PixelsPerMeter = 16.0f;  // screen texels per world meter (texture density is fixed at 16)

	// Stability
	bool bCameraSnap = true;
	bool bSubTexel = false;
	bool bObjectSnap = true;
	bool bBoneSnap = true;
	bool bRotationQuantize = true;
	bool bSteppedAnim = true;
	bool bChibi = false;     // style experiment, off: the project keeps normal proportions

	// Layering
	EPixelSortMode SortMode = EPixelSortMode::Full;
	EPixelCycleMode CycleMode = EPixelCycleMode::Merge;
	float FlattenK = 0.02f;

	// Shading
	bool bTrueLightPosition = true;
	bool bShadows = true;
	int32 ToonSteps = 3;
	EPixelOutlineMode OutlineMode = EPixelOutlineMode::Front;
	bool bLutCleanup = true;
	EPixelDitherMode DitherMode = EPixelDitherMode::Off;

	// Camera
	float PitchDeg = 41.41f; // cos = 0.75 -> a 1 m vertical edge is exactly 12 texels at 16 px/m
	float YawDeg = 0.f;      // camera yaw target; Q / R step it by YawStepDeg
	int32 YawStepDeg = 45;   // 45 like most top-down games; 30 and 90 selectable
	float YawExtraDeg = 0.f; // exhibits may add a fixed offset (the cycle exhibit uses 45)
	bool bAutoPan = false;
	EPixelFollow FollowMode = EPixelFollow::Layered;

	// Debug
	EPixelViewMode ViewMode = EPixelViewMode::Final;
	bool bShowSortVolumes = false;
	bool bShowGraph = false;
};

namespace PixelText
{
	const TCHAR* SortModeName(EPixelSortMode M);
	const TCHAR* FollowModeName(EPixelFollow M);
	const TCHAR* CycleModeName(EPixelCycleMode M);
	const TCHAR* OutlineModeName(EPixelOutlineMode M);
	const TCHAR* DitherModeName(EPixelDitherMode M);
	const TCHAR* ViewModeName(EPixelViewMode M);
}
