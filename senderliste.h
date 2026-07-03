#ifndef SENDERLISTE_H
#define SENDERLISTE_H

struct RadioStation {
    const char* name;
    const char* url;
};

// Deine Senderliste
const RadioStation radioStations[] = {
    {"Radio SAW", "http://stream.radiosaw.de/saw-harz-niedersachsen/mp3-192/"},
    {"Radio Brocken", "https://stream.radiobrocken.de/live/mp3-256/"},
    {"89.0 RTL", "http://stream.89.0rtl.de/live/mp3-256/"},
    {"SAW Party", "https://stream.radiosaw.de/saw-party/mp3-192/"},
    {"89.0 MIX", "https://stream.89.0rtl.de/mix/mp3-256/play.m3u"},
    {"SAW In The Mix", "https://stream.radiosaw.de/saw-in-the-mix/mp3-192/"},
    {"SAW Oldies", "https://stream.radiosaw.de/saw-oldies/mp3-192/"},
    {"89.0 XMAS", "https://stream.89.0rtl.de/christmas/mp3-256/"},
    {"SAW XMAS", "https://stream.saw-musikwelt.de/saw-weihnachten/mp3-128/stream.mp3"},
    {"SAW Schlager", "https://stream.radiosaw.de/saw-schlagerparty/mp3-192/"},
    {"WDR 1Live", "http://wdr-1live-live.icecast.wdr.de/wdr/1live/live/mp3/128/stream.mp3"}
};

#define STATION_COUNT (sizeof(radioStations) / sizeof(radioStations[0]))

#endif