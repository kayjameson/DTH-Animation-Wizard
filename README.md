# DTH Animation Wizard

Unreal Engine 5.8 editor plugin (upcoming). It batch-imports Daz animations onto DazToHue Genesis 9 characters, 1:1 with Daz, including multi-actor scenes, where roles are assigned to characters and the actors play in sync.

Each clip is sampled frame by frame in Daz Studio and recreated in Unreal with the DTH Pose Wizard's measured solve. This plugin depends on the DTH Pose Wizard plugin.

Status: Phase 1 (single-clip proof). The Daz clip sampler is `Resources/Daz/DTH_SampleAnimations.dsa`; the checks are the `DTH.Anim.*` automation tests.
