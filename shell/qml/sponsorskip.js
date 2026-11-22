// Runs inside youtube.com/tv, in the shell's own JavaScript world (the page's scripts don't
// see it). The shell hands over the segments to skip for the video playing
// (tvshellSetSegments, from YouTubePage.qml); this jumps over them and says so briefly.
(() => {
    if (globalThis.tvshellSetSegments)
        return;

    const names = {
        sponsor: "sponsor",
        selfpromo: "self-promotion",
        interaction: "reminder",
        intro: "intro",
        outro: "outro",
        preview: "recap",
        music_offtopic: "non-music part",
        filler: "filler",
    };
    let segments = [];
    let hideNote = 0;

    function note(text) {
        let box = document.getElementById("tvshell-skip-note");
        if (!box) {
            box = document.createElement("div");
            box.id = "tvshell-skip-note";
            box.style.cssText = "position:fixed;right:4vw;bottom:16vh;z-index:2147483647;"
                + "padding:1.2vh 2vh;border-radius:1.2vh;background:rgba(16,20,24,.9);color:#fff;"
                + "font:600 2.6vh sans-serif;transition:opacity .2s;pointer-events:none";
            document.documentElement.appendChild(box);
        }
        box.textContent = text;
        box.style.opacity = "1";
        clearTimeout(hideNote);
        hideNote = setTimeout(() => { box.style.opacity = "0"; }, 3000);
    }

    globalThis.tvshellSetSegments = list => {
        segments = Array.isArray(list) ? list : [];
        console.log("tvshell: " + segments.length + " segments to skip");
    };

    setInterval(() => {
        if (segments.length === 0)
            return;
        const video = document.querySelector("video.html5-main-video") || document.querySelector("video");
        if (!video || video.paused || !isFinite(video.duration))
            return;
        const now = video.currentTime;
        for (const segment of segments) {
            // Not the last moments of a segment: seeking lands slightly early at times.
            if (now >= segment.start && now < segment.end - 0.5) {
                video.currentTime = Math.min(segment.end, video.duration);
                note("Skipped " + (names[segment.category] || segment.category));
                console.log("tvshell: skipped " + segment.category + " " + segment.start + "-" + segment.end);
                break;
            }
        }
    }, 250);
})();
