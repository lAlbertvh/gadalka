#pragma once

#include <string>
#include <vector>
#include <optional>

namespace jyotish::feedback {

// Feedback hook: after a full oracle reply the server appends a "[FEEDBACK|..]"
// marker plus a visible "was this useful?" question. On the NEXT user turn a
// plain "yes"/"no" classifies the just-past (question, answer) pair, which is
// appended to <feedback_dir>/YYYY-MM-DD.jsonl for possible future fine-tuning.
// The marker lives inside the assistant message the same way [CONFIRM| does,
// so a reply is never double-asked. The marker also encodes whether the answer
// was research-grounded (g1/g0), so the future dataset knows exactly which
// engine line produced the answer.

// Marker that keeps the feedback ask together with the assistant message.
// `grounded` = whether this answer was corroborated by the live research step.
std::string marker(bool grounded);

// The marker line appended after a full oracle reply. The frontend turns it
// into a 👍/👎 widget, so no visible "answer yes/no" text is sent anymore. The
// marker alone is enough for pending()/strip() to keep working; a user who
// still types "да"/"нет" hits the classify()/is_pure_rating() safety net.
std::string ask(const std::string& lang, bool grounded);

// True when the last assistant message in history carries a feedback marker
// and no assistant message followed it (i.e. the ask is still unanswered).
// Returns the grounded flag stored in that marker when present.
bool pending(const std::vector<std::string>& history_messages, bool* grounded_out);

// The user's answer to a pending feedback ask: returns 1 for "yes"-equivalent,
// -1 for "no", 0 when the message is not a feedback answer at all (so the
// normal oracle flow continues).
int classify(const std::string& text, const std::string& lang);

// True only when the message is *nothing but* a rating ("да", "нет, не помог")
// — short, no question mark, no question words. Anything else (e.g. "да, а
// расскажи кого мне лучше полюбить?") must NOT be swallowed by the feedback
// hook; it is a real question and the oracle has to answer it in full. The
// server records a rating only when this returns true.
bool is_pure_rating(const std::string& text, const std::string& lang);

// Strip the marker + ask text from an assistant reply (for LLM history reuse
// and for recording the clean answer). A plain reply without a marker is
// returned unchanged.
std::string strip(std::string reply);

// Extract the (question, answer) pair belonging to the last feedback ask:
// the previous user message and the assistant message with the marker removed.
std::optional<std::pair<std::string, std::string>> pair_for_last_ask(
    const std::vector<std::string>& history_messages);

// Append one JSONL record to <feedback_dir>/YYYY-MM-DD.jsonl (creates the dir).
// `useful` true on a positive rating, false on a negative one. Never throws
// (best-effort; a missing disk just does nothing). `model` and `grounded` are
// recorded so the future dataset knows exactly which engine line produced it.
void record(const std::string& feedback_dir,
            const std::string& lang,
            const std::string& question,
            const std::string& answer,
            bool useful,
            const std::string& model,
            bool grounded);

} // namespace jyotish::feedback