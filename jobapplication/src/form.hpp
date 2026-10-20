// The job application as a form: its title, its sections, and in them the fields that will be filled in. Every field
// has a key, the name chat will use for it ("name", "position", "experience", "why_hire", ...), and a value that is
// empty until something writes in it. The words printed on it are the plugin's strings (Form.* in en-US.ini).
//
// paper.hpp paints it. Filling a field and painting again is all it takes to show what was written.
#pragma once

#include <QString>
#include <QStringList>

#include <string>
#include <vector>

namespace sarxina::jobapplication {

struct Field {
	enum class Kind {
		Line,   // a label and a line to write on, as wide as its share of the row
		Lines,  // ruled lines across the whole row, under the section's heading
		Choice, // a label and a box for each option; the value is the option ticked
	};
	std::string key;
	Kind kind = Kind::Line;
	QString label;
	double share = 1; // of the row's width
	int lines = 1;    // Lines: how many
	QStringList options;
	QString value;
};

struct Row {
	std::vector<Field> fields;
};

struct Section {
	QString heading;
	QString note; // small print under the heading, or empty
	std::vector<Row> rows;
};

struct Form {
	QString title;
	QString note;
	std::vector<Section> sections;
	QString footer;

	// The field with this key, or nullptr.
	Field *field(const std::string &key);
	const Field *field(const std::string &key) const;
	std::vector<std::string> keys() const;

	// Writes text in a field, made safe to show (plain-text.hpp) and cut to what fits. A Choice takes only one of its
	// options, in any case. False if there is no such field or the text does not fit it.
	bool set(const std::string &key, const QString &text);
	void clear(); // every field empty again
};

// The blank job application.
Form jobApplication();

// How long a value may be, in characters.
int longestValue(const Field &field);

} // namespace sarxina::jobapplication
