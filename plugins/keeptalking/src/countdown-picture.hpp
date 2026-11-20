// The countdown's number as a picture, in Sarxina's caption look: Luckiest Guy (Arial Black where it is not
// installed), amber with a navy outline and a hard navy shadow straight down. Premultiplied ARGB, transparent around it.
#pragma once

#include <QImage>
#include <QString>

namespace sarxina::keeptalking {

// The font the number is drawn in. Call it on the UI thread first; after that it is safe anywhere.
QString countdownFont();

// START YAPPING OR ELSE over the number, sized for the canvas: the digits about 45% of its height, the line about 12%
// (smaller if it would not fit across).
QImage paintNumber(int number, int canvasWidth, int canvasHeight);

} // namespace sarxina::keeptalking
