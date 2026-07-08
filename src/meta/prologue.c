#include "prologue.h"
#include <stddef.h>

/* Speakers. Maya is on the headset; the player character is unnamed, so
 * their lines read as "YOU". */
#define MAYA  "MAYA"
#define YOU   "YOU"

static const dlg_line_t scene01[] = {
    { NULL, "MOUNT XERXES: BASE CAMP\nScene 01 - The Morning After", DLG_CAPTION },

    { NULL, "A dying campfire crackles. A kettle hisses toward the boil. Nylon tent fabric stirs in the alpine breeze.", DLG_NARRATION },
    { NULL, "Your eyes open. Frost clings to the canvas. Beyond the half-open flaps a bruised twilight sky bleeds into dawn gold - and Mt. Xerxes, massive and serene, fills the world.", DLG_NARRATION },

    { NULL, "[ Push forward to leave the tent ]", DLG_PROMPT },
    { NULL, "Boots crunch on frost-hard earth. The wind carries the distant groan of shifting ice.", DLG_NARRATION },

    { NULL, "[ Interact to pour the tea ]", DLG_PROMPT },
    { NULL, "Steam curls off the enamel mug. A gentle jingle cuts the quiet - a headset connecting, wrapped in static.", DLG_NARRATION },

    { MAYA, "Hey. Tracking beacon says you're awake. Or at least, your backpack is.", DLG_SPEECH },
    { YOU,  "I'm awake, Maya. Just pouring some tea.", DLG_SPEECH },
    { MAYA, "Good. It's freezing out there. Did you sleep at all?", DLG_SPEECH },
    { YOU,  "A few hours. The fire kept me company.", DLG_SPEECH },

    { NULL, "You look up. The morning sun catches the highest ridge and sets it burning orange.", DLG_NARRATION },
    { YOU,  "It's bigger than it looked in the brochures.", DLG_SPEECH },
    { MAYA, "Yeah, well - Xerxes doesn't mess around. Neither do you, apparently. I still can't believe you're actually out there. You didn't have to do this alone. I could have taken the time off. We could have done the lower trails.", DLG_SPEECH },
    { YOU,  "I know. Thank you. But I need it to be just me right now. Me, the rock, and you in my ear.", DLG_SPEECH },
    { MAYA, "You've been doing the solo act since you were nine years old. You're allowed to let people carry the weight with you sometimes, you know.", DLG_SPEECH },

    { NULL, "A slow sip of tea. A long, steadying exhale.", DLG_NARRATION },
    { YOU,  "I'm okay, Maya. Really. The silence out here - it's loud, but it's good.", DLG_SPEECH },
    { MAYA, "Okay. I won't push. How's the tea?", DLG_SPEECH },
    { YOU,  "Terrible. I brought his favourite blend. That awful Earl Grey with the dried lavender.", DLG_SPEECH },
    { MAYA, "Oh. He would have loved that you brought it. Though he'd have made you drink the whole pot while lecturing you about the tannins.", DLG_SPEECH },
    { YOU,  "Yeah. He would have. ...I'm going to take it up there, Maya. To the summit. Like we planned - before the diagnosis.", DLG_SPEECH },
    { MAYA, "Take your time with it. The mountain isn't going anywhere. Four major rest points on your route - check in with me when you reach the first ridge, okay?", DLG_SPEECH },
    { YOU,  "Will do.", DLG_SPEECH },
    { MAYA, "Be safe. Talk soon.", DLG_SPEECH },

    { NULL, "Beep. The headset drops, and the vast acoustics of the valley rush back in. The wind whispers through the pines.", DLG_NARRATION },
    { NULL, "[ Interact to pack up camp ]   [ C-Up to approach the wall ]", DLG_PROMPT },
};

const dlg_line_t *prologue_scene(int *count) {
    *count = (int)(sizeof scene01 / sizeof scene01[0]);
    return scene01;
}
