#include "form.hpp"

#include "plain-text.hpp"

#include <obs-module.h>

namespace sarxina::jobapplication {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

Field line(const char *key, const char *label, double share)
{
	Field field;
	field.key = key;
	field.kind = Field::Kind::Line;
	field.label = text(label);
	field.share = share;
	return field;
}

Field lines(const char *key, int count)
{
	Field field;
	field.key = key;
	field.kind = Field::Kind::Lines;
	field.lines = count;
	return field;
}

Field choice(const char *key, const char *label, std::initializer_list<const char *> options)
{
	Field field;
	field.key = key;
	field.kind = Field::Kind::Choice;
	field.label = text(label);
	for (const char *option : options)
		field.options << text(option);
	return field;
}

} // namespace

Form jobApplication()
{
	Form form;
	form.title = text("Form.Title");
	form.note = text("Form.Note");
	form.footer = text("Form.Footer");

	Section applicant{text("Form.Applicant"), {}, {}};
	applicant.rows.push_back({{line("name", "Form.Name", 0.66), line("date", "Form.Date", 0.34)}});
	applicant.rows.push_back({{line("position", "Form.Position", 1)}});
	applicant.rows.push_back({{line("pay", "Form.Pay", 0.5), line("start", "Form.Start", 0.5)}});
	applicant.rows.push_back(
		{{choice("hours", "Form.Hours", {"Form.Hours.Full", "Form.Hours.Part", "Form.Hours.Seasonal"})}});
	form.sections.push_back(applicant);

	form.sections.push_back({text("Form.Experience"), text("Form.Experience.Note"), {{{lines("experience", 3)}}}});
	form.sections.push_back({text("Form.WhyHire"), {}, {{{lines("why_hire", 4)}}}});

	Section certification{text("Form.Certification"), text("Form.Certification.Note"), {}};
	certification.rows.push_back(
		{{line("signature", "Form.Signature", 0.64), line("signed_date", "Form.Date", 0.36)}});
	form.sections.push_back(certification);
	return form;
}

Field *Form::field(const std::string &key)
{
	for (Section &section : sections)
		for (Row &row : section.rows)
			for (Field &f : row.fields)
				if (f.key == key)
					return &f;
	return nullptr;
}

const Field *Form::field(const std::string &key) const
{
	return const_cast<Form *>(this)->field(key);
}

std::vector<std::string> Form::keys() const
{
	std::vector<std::string> all;
	for (const Section &section : sections)
		for (const Row &row : section.rows)
			for (const Field &f : row.fields)
				all.push_back(f.key);
	return all;
}

int longestValue(const Field &field)
{
	switch (field.kind) {
	case Field::Kind::Line:
		return field.share >= 1 ? 48 : 28;
	case Field::Kind::Lines:
		return 70 * field.lines;
	case Field::Kind::Choice:
		return 25;
	}
	return 0;
}

bool Form::set(const std::string &key, const QString &text)
{
	Field *f = field(key);
	if (!f)
		return false;
	const QString clean = plainText(text, longestValue(*f));
	if (f->kind == Field::Kind::Choice) {
		for (const QString &option : f->options)
			if (option.compare(clean, Qt::CaseInsensitive) == 0) {
				f->value = option;
				return true;
			}
		return clean.isEmpty() ? (f->value.clear(), true) : false;
	}
	f->value = clean;
	return true;
}

void Form::clear()
{
	for (Section &section : sections)
		for (Row &row : section.rows)
			for (Field &f : row.fields)
				f.value.clear();
}

} // namespace sarxina::jobapplication
