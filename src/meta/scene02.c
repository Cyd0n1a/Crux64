#include "scene02.h"
#include <stdio.h>
#include <stddef.h>

/* Speakers, as in prologue.c: the player character stays unnamed, so their
 * lines read as "YOU" even once Maya starts calling them Kit. */
#define MAYA  "MAYA"
#define YOU   "YOU"

/* Maya reads the tracker altitude aloud. dialogue_start() keeps the line
 * array by pointer and the text with it (dialogue.h:41), so this buffer is
 * file-scope static — a stack local would dangle the moment the scene ran. */
static char alt_line[128];

static const dlg_line_t scene02[] = {
    { NULL, "MOUNT XERXES: CAMP TWO\nScene 02 - Crepuscular Twilight Stars", DLG_CAPTION },

    { NULL, "Night has closed over the outcropping. Your bones ache with the whole of the day in them, and the air has gone so still it feels like the mountain is holding its breath.", DLG_NARRATION },
    { NULL, "You unfurl the bedroll from your pack and lay the sleeping bag out on top of it.", DLG_NARRATION },

    { NULL, "The earpiece beeps. You reach up and thumb the answer button.", DLG_NARRATION },

    { YOU,  "Hey, Maya. I've made it to the first checkpoint. You're right on time.", DLG_SPEECH },
    { MAYA, "Hey there, my little acrobat! I'm quite soggy and wrapped in fluffy...", DLG_SPEECH },
    { YOU,  "Towels or hugs?", DLG_SPEECH },
    { MAYA, "Hah! I wish we were together right now. I was about to get high in my own way...", DLG_SPEECH },
    { YOU,  "Of course. Edibles, or..?", DLG_SPEECH },
    { MAYA, "Wouldn't you like to know! Heh. Well - both are chewable. Anyhow. How are you doing?", DLG_SPEECH },
    { YOU,  "A little scratched up. Scuffed knees. The chalk's mixing with the blood in the wrappings on my hands.", DLG_SPEECH },
    { YOU,  "You should see the stars from here, Maya. The milky beauty of it all. I wish I could remember that Greek myth - the one about the mother's tears?", DLG_SPEECH },

    { NULL, "The sat-link crackles and pops. Handover protocol; a minor glitch in the signal.", DLG_NARRATION },

    { YOU,  "...Ahem. Sooo, yeah. I'd better get started lighting the propane and building a fire for the night.", DLG_SPEECH },
    { MAYA, alt_line, DLG_SPEECH },
    { MAYA, "Keep at it. We're all equally terrified and cheering you on from here. Just try to stay safe, okay?", DLG_SPEECH },
    { YOU,  "Of course. Of course. Jeez, don't worry (*cough*) so much! You city types. Always so much going on, so much anxiety...", DLG_SPEECH },
    { MAYA, "Yeeeaaah. Toronto, amirite. Sigh.", DLG_SPEECH },

    { NULL, "She takes a deep breath. You hear the soft clink of a wine glass set down on a table, sirens, wheels humming over tarmac - all of it from oh so far away.", DLG_NARRATION },

    { YOU,  "Well. I have to preserve battery for the night. Be well, Maya, my dear friend.", DLG_SPEECH },
    { MAYA, "Oh! Well, if you... Be well. TTYL.", DLG_SPEECH },
    { YOU,  "TTYL. Toodle pip and all that jazz.", DLG_SPEECH },

    { NULL, "A wry smile crosses your lips. Nobody is here to see it, apart from a scruffy young owl in a nearby tree.", DLG_NARRATION },

    { MAYA, "Hey! You absolute loon, when you get back h-", DLG_SPEECH },

    { NULL, "Click.", DLG_NARRATION },

    { YOU,  "Ahhhh.", DLG_SPEECH },
    { NULL, "You sit down on the sleeping bag, just for a moment, and reach for the mini propane tank. The spigot roars into a bright blue flame, throwing new shadows off the rocks around you. You set the kettle over it.", DLG_NARRATION },
    { NULL, "You lay back under the stars, and begin to...", DLG_NARRATION },

    { NULL, "[ Press A to begin meditation ]", DLG_PROMPT },
};

const dlg_line_t *scene02_scene(float alt_m, int *count) {
    /* The route tops out around 480 m, so the tracker reads metres — Maya
     * saying "0.12 kilometres" would be a stranger line than the script's
     * intent. */
    if (alt_m < 0.f) alt_m = 0.f;
    snprintf(alt_line, sizeof alt_line,
             "Well, just so long as you're okay. The tracker shows you've "
             "reached %d metres!", (int)(alt_m + 0.5f));

    *count = (int)(sizeof scene02 / sizeof scene02[0]);
    return scene02;
}
