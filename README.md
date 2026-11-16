# DTH Animation Wizard

An Unreal Engine 5.8 editor plugin. It brings Daz Studio animations onto DazToHue Genesis 9 characters 1:1 with Daz, including multi-actor scenes: you give each role a character, and the actors play in sync.

Each clip is sampled frame by frame in Daz Studio and recreated in Unreal with the DTH Pose Wizard's measured solve (its `DTHPoseCore` module). This plugin depends on the DTH Pose Wizard plugin, 0.4.0 or later (its face fit, `DTHFaceCore`, carries clip faces) (https://github.com/kayjameson/DTH-Pose-Wizard/releases): install both in your project's `Plugins` folder.

## Use

1. **Sample the clips in Daz Studio.** Open `Resources/Daz/DTH_SampleAnimations.dsa` in Daz Studio (File > Open, or double-click it). The script:
   - loads a fresh Genesis 9 and records DTH's rest pose (UnrealPose)
   - applies every clip in an `Animations` folder under `People/Genesis 9*` and records every frame
   - does the same for clips under `People/Genesis 8*`, carried onto Genesis 9 (see below). This needs Genesis 8 Female and Male Starter Essentials.
   - records the clip's face on every frame where it keys one (eyes, lids, jaw, lips, tongue, brows): a second,
     never-animated Genesis 9 stands beside the clip's figure, takes the clip's face values frame by frame and is
     measured as the Expression Wizard measures an expression (`face.json`, `clips/<clip>.face.txt`). A clip that keys
     no face costs nothing more. `args.face: false` leaves faces out; `args.testFace` keys a face test clip.
   - writes `DTH_AnimationSamples.json` to `Documents/DAZ 3D/Studio4/DTH Animation Wizard`

   Nothing is saved in Daz. A run that stops carries on where it left off. One set of samples serves every DTH Genesis 9 character.
2. **Open Tools > DazToHue > DTH Animation Wizard.**
   - **Settings:** check *Daz Samples Index* and *Sequence Folder*.
   - **Clips:** click a clip to preview it, tick clips, pick a character, and write. Each clip becomes `Sequence Folder/<character>/<pack>/A_<clip>`: one key per Daz frame at the clip's frame rate, bone tracks, and the clip's face when it keys one: fitted on the character frame by frame with the shared face fit (`DTHFaceCore`, each frame starting from the one before), as face-board and morph curves plus the eyes, jaw and tongue turned past DTH's face rig.
   - **Scenes:** clips made to play together (same pack, names differing only in the last word, e.g. `... Female` and `... Male`). Pick a scene, give each role a character, and build. Each role's sequence is written for its character, plus `Sequence Folder/Scenes/<pack>/LS_<scene>`, a level sequence that plays them together from frame 0. Every actor starts at the sequence's origin: the samples carry their placement relative to each other.
   - **Preview** (on both pages): the clip, or every cast role of the scene, plays on its character in the wizard's own preview scene, with play/pause, a scrubber, loop, and front, side and three-quarter views. It plays exactly what the written sequence plays (the same solve, through the character's post-process blueprint). Nothing is written.
   - **Speed** (under the preview): how fast the clip or scene plays, 0.1 to 4. Writing keeps it as each sequence's Rate Scale, so the frames are unchanged and the sequence plays at that speed everywhere, Sequencer included. A scene's speed applies to every role, and its tracks and playback range are stretched to match, so the actors stay in sync.
3. **Play** a sequence on the character's body mesh (Animation Mode "Use Animation Asset", or an Animation track in Sequencer). Keep the character's DTH post-process blueprint on: it supplies the twist bones and the joint correctives.

### Genesis 8 clips

A Genesis 8 clip plays on a base Genesis 8 loaded beside the Genesis 9. On every frame, each G9 bone is turned to match its G8 bone in world space, and the hip travels as G8's does, scaled by the two figures' hip heights. G9 is then recorded like one of its own clips, so a G8 clip works like any G9 clip everywhere after that. The Clips page marks it "from Genesis 8 Female" (or Male).
- **Bone map.** G8's bend/twist pairs map onto G9's single bones (`lShldrBend`+`lShldrTwist` → `l_upperarm`, and the same for forearms and thighs). G8's `abdomen`/`chest` bones map to `spine1`–`4`, `Carpal1`–`4` to the metacarpals, and `SmallToe1`–`4` to the toes.
- **Rest alignment.** Base G8 and base G9 don't share a rest pose: G8's legs are about 6° wider, for example. Each G9 bone is first turned to point where G8's points, then twisted so its bend axes line up with G8's. So G9 makes G8's shapes, not just G8's changes.
- **Joint limits.** G9's limits are off while a G8 clip plays. G8 clips go past them (clip 001 bends G8's knee 28° sideways where G9 allows 5°), and following G8's movement is the point. The run report lists each clip's channels past G9's limits.
- **Per clip, the report and the samples say** how closely G9 followed G8:
  - bone rotations: within 1.5–4.5° on clip 001. The only gaps are G9's locked toe channels.
  - bone directions: within 2.4°. The hand's figure includes a fixed offset from where the two figures' middle fingers start.
  - ankle heights against G8's, scaled: within 2.8 cm. The two figures' leg proportions differ.
- The vendor's own G9 versions of these clips aren't the same motion as their G8 originals: forearms are about 20–26° off, and fingers up to 31°. So the G8 clip is checked against G8 itself, not against the G9 version.

Scripting (Python `unreal.DTHAnimScripting`, or Blueprints): `OpenWizard`, `ListClips`, `WriteClips`, `ListScenes`, `BuildScene`. `WriteClips` and `BuildScene` take an optional `Speed`.

## What it is measured against

The `DTH.Anim.*` automation tests. Test assets go only under the settings' Test Folder (`/Game/DTH_Animations/_Test`).
- `P1.UnrealPoseMatchesPoseWizard`: the sampler's rest is the Pose Wizard's.
- `P1.ClipRecreated`: every frame, played on the character through its post-process blueprint, against the same clip Daz evaluated on the character itself. Every joint within 0.5 cm and every non-twist bone within 0.5°. Between keys, the sequence stays with its keys.
- `P1.Screenshots`: body front, side and back; face front and three-quarter; mouth.
- `P2.Batch`, `P2.Wizard`: batch writing, replacing, and the tab (with a screenshot of each page).
- `P3.Pairs`, `P3.SameAsOneScene`, `P3.RelativePlacement`, `P3.LevelSequence`: scenes, sync and the actors' placement relative to each other.
- `G8.CarriedOntoGenesis9`, `G8.ClipRecreated`, `G8.Screenshots`: a Genesis 8 clip carried onto Genesis 9 in Daz (how closely G9 followed G8), then recreated on the character against the same carry evaluated on the character in Daz, by the Phase 1 rule, plus screenshots.
- `Preview.MatchesSequence`, `Preview.Speed`: the preview against the written sequence on the character (every bone, on and between frames, within 0.05 cm and 0.05°), speed written as Rate Scale, a scene's tracks and range stretched to its speed, plus screenshots.
- `Measure.Smoothness`: not a check. How the test clips move between frames (8 points per frame through the post-process blueprint, and the preview at 60 fps), per bone and curve, to `Saved/DTHAnimationWizard/smoothness.tsv`.
- `Setup.ImportTestCharacter`: imports the test character with the DTH Import Wizard (`-DTHAnimImportExport=<export folder>`).

## Known limits (DTH's or the engine's, reported, not corrected)

- **Pec clavicles.** DTH's Skeleton translation retargeting on the pec clavicles keeps their bind translation, so a played frame can be up to about 0.6 cm off there.
- **Twist bones.** DTH's post-process blueprint solves the twist bones from the hand, so they differ from Daz's keyed twist bones.
- **Figure scale.** A character's Daz figure scale (Vi: 0.99333) is not in the DTH export, so the hip travels by base Genesis 9's distance. That's about 0.3 cm on 48 cm of travel.
- **Faces.** A clip's face is carried when it was sampled with this sampler (0.2.0); body shape morphs are not, and are reported. None of the installed Genesis 9 clips animates the face (they key it to rest on frame 0). The face path is checked on a face test clip (`DTH.Anim.Face.Clip`, 2026-11-16): every face region within 0.4 mm on the measured frames except a few gum points, and the eyes turned 28.90 deg where Daz turns them 29.09 deg, past DTH's 20 deg eye-look limit. A clip that keys a pack's expression morph Daz loads on demand loses that part of its face (the second figure does not have the morph).
- **Contacts between actors.** If a character's proportions differ from base Genesis 9's, contacts between actors (hands, embraces) may not meet exactly.
