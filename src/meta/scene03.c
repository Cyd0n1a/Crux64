#include "scene03.h"
#include <stddef.h>

#define MAYA  "MAYA"
#define YOU   "YOU"

/* The eagle's stoop is a set-piece in the script with no entity behind it
 * yet, so it plays here as narration. If a real bird ever flies it, these
 * lines become the beats to sync the camera against. */
static const dlg_line_t scene03[] = {
    { NULL, "MOUNT XERXES: CAMP TWO\nScene 03 - The Circling Hour", DLG_CAPTION },

    { NULL, "Light comes back slowly, like an eyelid easing open. The night's weight has lifted. Pale blue morning washes over the ledge, and condensation beads along the outside of the sleeping bag.", DLG_NARRATION },
    { NULL, "Total stillness, at first. Then the tick of cooling propane, a single bird call somewhere far below, the low moan of wind moving through a distant col.", DLG_NARRATION },

    { NULL, "[ Push forward to sit up ]", DLG_PROMPT },
    { NULL, "You sit up. The horizon holds you a beat longer than you meant it to - the sun still hasn't cleared the ridge.", DLG_NARRATION },

    { NULL, "[ Interact to check your gear ]", DLG_PROMPT },
    { NULL, "You run a hand along the frost-stiffened straps of the pack, checking buckles out of habit more than need. The earpiece connects with its soft static bloom.", DLG_NARRATION },

    { MAYA, "Morning, mountain goat. Vitals look steady. How'd the sit go?", DLG_SPEECH },
    { YOU,  "Quiet. Quieter than I expected. My head usually won't shut up, but out here it just... did.", DLG_SPEECH },
    { MAYA, "Good. That's good, Kit.", DLG_SPEECH },
    { MAYA, "Sorry - that's a new one. Camp-two-you sounds different than base-camp-you. Softer.", DLG_SPEECH },
    { YOU,  "Softer, huh. Don't get used to it. Altitude's just knocked the sarcasm out of me temporarily.", DLG_SPEECH },
    { MAYA, "There it is. Okay, talk me through the view. I need the postcard version - I'm stuck in a meeting in forty minutes.", DLG_SPEECH },
    { YOU,  "Sky's gone that pale, watery blue. Like it hasn't decided on a colour yet. Rock's rust-red where the sun's starting to touch it.", DLG_SPEECH },
    { YOU,  "And Xerxes just... keeps going up. I thought yesterday's view was the whole mountain. It wasn't even a third of it.", DLG_SPEECH },
    { MAYA, "He'd have had something poetic to say about that. You know - the \"the summit humbles you twice\" thing he always said. Once from the bottom, once from the-", DLG_SPEECH },
    { YOU,  "Once from the top. Yeah. I remember.", DLG_SPEECH },
    { YOU,  "I keep waiting to feel him here. Haven't yet. Maybe higher up.", DLG_SPEECH },
    { MAYA, "Maybe. Or maybe he's just letting you have the quiet first.", DLG_SPEECH },

    { NULL, "A shadow sweeps across the ledge and is gone as fast as it came. You look up.", DLG_NARRATION },
    { NULL, "High above, riding a thermal along the cliff face, a golden eagle banks into view - wings still, angled, catching the rising air. Even at this distance it is enormous. A dark, deliberate shape against a pale sky.", DLG_NARRATION },

    { NULL, "[ Look up to track the eagle ]", DLG_PROMPT },
    { NULL, "The wind drops away, as though the whole valley were holding its breath. A faint, high, keening cry drifts down.", DLG_NARRATION },

    { YOU,  "Maya. There's an eagle. Huge - must be a golden. It's just hanging there, barely moving.", DLG_SPEECH },
    { MAYA, "Send a picture if you can. Actually - no, don't, save the battery. Just tell me about it.", DLG_SPEECH },

    { NULL, "The eagle tilts, one wingtip dipping. Its circling tightens, spiralling lower down the cliff face in slow, unhurried loops.", DLG_NARRATION },

    { YOU,  "It's circling something. Getting lower each pass. There - on that scree slope, maybe two hundred metres down. A marmot, out sunning itself. Doesn't even know.", DLG_SPEECH },
    { MAYA, "You don't have to narrate this part if you don't want to.", DLG_SPEECH },
    { YOU,  "No - no, I want to watch. It feels wrong to look away.", DLG_SPEECH },

    { NULL, "The circling stops. For a long moment the bird simply hangs on the thermal, motionless, calculating. Then, without any visible transition, it folds its wings and drops.", DLG_NARRATION },
    { NULL, "The stoop is impossibly fast - a controlled, plummeting line, wings tucked to a blade. At the last instant they snap open, talons forward, and the shape disappears behind a fold of rock. A single sharp cry. Then silence.", DLG_NARRATION },

    { YOU,  "...Got it, I think. Didn't see the end of it. Rock was in the way.", DLG_SPEECH },
    { MAYA, "That's the mountain too, you know. Not just the pretty light on the ridge. The whole of it.", DLG_SPEECH },
    { YOU,  "Yeah. I don't know why, but that actually helps somehow. Everything up here just... does what it's built to do. No hesitation.", DLG_SPEECH },
    { MAYA, "Maybe borrow a little of that today. Less hesitation, more moving forward.", DLG_SPEECH },
    { YOU,  "Noted, coach.", DLG_SPEECH },
    { MAYA, "I've got to run - this meeting won't chair itself. Second ridge is your next checkpoint. Eat something before you climb, please. Actual food, not just his tea.", DLG_SPEECH },
    { YOU,  "Yes, ma'am. Talk soon, Maya.", DLG_SPEECH },
    { MAYA, "Talk soon. Go get humbled by a mountain, mountain goat.", DLG_SPEECH },

    { NULL, "Beep. The line drops, and the wind fills back in around the silence. Somewhere far below, a single feather drifts down onto the scree.", DLG_NARRATION },
    { NULL, "[ Interact to eat rations ]   [ C-Up to climb for Ridge Two ]", DLG_PROMPT },
};

const dlg_line_t *scene03_scene(int *count) {
    *count = (int)(sizeof scene03 / sizeof scene03[0]);
    return scene03;
}
