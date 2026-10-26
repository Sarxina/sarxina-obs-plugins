// The job application as paper, painted into the picture VTube Studio shows as an item.
//
// The sheet is letter-shaped, off-white with a grain, a few fibres and two folds from being folded in three, its ink
// laid out as a printed form (form.hpp). It is drawn into the picture slightly out of square, as a hand-held sheet is,
// with a soft shadow under it.
//
// VTube Studio pins an item by the centre of its picture. The picture has room above the sheet, so that its centre is
// a third of the way down the sheet: pinned to the middle of the chest, the sheet starts at the bottom of the neck and
// covers the body below it, and it turns about the chest as the body leans.
#pragma once

#include "form.hpp"

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QSize>

#include <cstdint>

namespace sarxina::jobapplication {

// The sheet, flat, and where it is in the picture.
inline constexpr int kSheetWidth = 1097;
inline constexpr int kSheetHeight = 1420; // letter: 8.5 x 11, as large as fits 2048 with the room above
inline constexpr double kPinDown = 0.32;  // the picture's centre, as a share of the sheet's height from its top
QSize pictureSize();
QRectF sheetInPicture(); // before it is drawn out of square

// VTube Studio takes pictures of 64 to 2048 pixels a side and less than 5 MB.
inline constexpr int kLargestSide = 2048;
inline constexpr qsizetype kLargestPicture = 5 * 1024 * 1024;

// The flat sheet with the form on it. The same seed gives the same grain and fibres.
QImage paintSheet(const Form &form, uint32_t seed);
// The picture VTube Studio gets: the sheet, out of square, and its shadow.
QImage paintPicture(const Form &form, uint32_t seed);
QImage paintPicture(const QImage &sheet); // from a sheet painted already
// As a PNG file; `quick` packs it less tightly, in about a third of the time, for one written again and again.
QByteArray png(const QImage &picture, bool quick = false);

// What a chatter wrote, or their name, as it can be written on the paper: the characters that turn text around or hide
// in it taken out (as plain-text.hpp does), and also every character that none of the paper's fonts has, which would
// show as an empty box. Emoji, and letters of other scripts that Windows has fonts for, stay. Spaces are tidied, and
// it is cut to at most `longest` characters (a whole emoji or accented letter counts as one), with "…" where it was.
QString writable(const QString &text, int longest);

} // namespace sarxina::jobapplication
