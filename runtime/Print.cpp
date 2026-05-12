#include "EvaluatorImpl.hpp"
#include <climits>
#include <cstdlib>

// Value printing, modelled on R's print.default: numbers get 7 significant digits with a
// shared layout per vector/column, columns are aligned, and long vectors wrap at 80 chars.

namespace Evaluator {

    static const int kPrintDigits = 7;  // options(digits = 7)
    static const int kPrintWidth = 80;  // options(width = 80)

    static MiniString Spaces(int n) {
        MiniString s;
        for (int i = 0; i < n; ++i) s.push_back(' ');
        return s;
    }
    static MiniString PadLeft(const MiniString& s, int w) {
        int n = w - (int)s.size();
        return n > 0 ? Spaces(n) + s : s;
    }
    static MiniString PadRight(const MiniString& s, int w) {
        int n = w - (int)s.size();
        return n > 0 ? s + Spaces(n) : s;
    }

    // --- Numbers ---

    // Significant digits needed (at most kPrintDigits) and the decimal exponent of x
    // after rounding it to kPrintDigits significant digits.
    static void SigDigits(double x, int& nsig, int& kpower) {
        if (x == 0.0) { nsig = 1; kpower = 0; return; }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*e", kPrintDigits - 1, std::fabs(x)); // "d.dddddde+XX"
        int nd = 1, last_nonzero = 1;
        for (const char* p = buf + 2; *p >= '0' && *p <= '9'; ++p) {
            ++nd;
            if (*p != '0') last_nonzero = nd;
        }
        nsig = last_nonzero;
        const char* e = std::strchr(buf, 'e');
        kpower = e ? std::atoi(e + 1) : 0;
    }

    // One layout shared by a set of doubles (R's formatReal): fixed notation with a common
    // number of decimals, or scientific with a common mantissa, whichever is narrower.
    struct RealFormat {
        bool sci = false;
        int digits = 0; // decimals in fixed notation, or mantissa decimals in scientific
    };

    static RealFormat ChooseRealFormat(const MiniVector<double>& xs) {
        RealFormat f;
        bool any = false, any_neg = false;
        int mxsl = 1, rgt = 0, mxns = 1, mxe = INT_MIN, mne = INT_MAX;
        for (double x : xs) {
            if (std::isnan(x) || std::isinf(x)) continue;
            int nsig, kp;
            SigDigits(x, nsig, kp);
            bool neg = x < 0;
            any = true;
            any_neg |= neg;
            int left = kp + 1;
            mxsl = std::max(mxsl, (neg ? 1 : 0) + (left <= 0 ? 1 : left));
            rgt = std::max(rgt, std::max(0, nsig - kp - 1));
            mxns = std::max(mxns, nsig);
            mxe = std::max(mxe, kp);
            mne = std::min(mne, kp);
        }
        if (!any) return f;
        int fixed_width = mxsl + rgt + (rgt ? 1 : 0);
        int exp_width = (mxe >= 100 || mne <= -100) ? 5 : 4; // e+05 / e+100
        int sci_width = (any_neg ? 1 : 0) + (mxns > 1 ? mxns + 1 : mxns) + exp_width;
        if (fixed_width <= sci_width) {
            f.digits = rgt;
        } else {
            f.sci = true;
            f.digits = mxns - 1;
        }
        return f;
    }

    static MiniString FormatReal(double x, const RealFormat& f) {
        if (IsNAReal(x)) return "NA";
        if (std::isnan(x)) return "NaN";
        if (std::isinf(x)) return x > 0 ? "Inf" : "-Inf";
        if (x == 0.0) x = 0.0; // R prints -0 as 0
        char buf[128];
        std::snprintf(buf, sizeof(buf), f.sci ? "%.*e" : "%.*f", f.digits, x);
        return MiniString(buf);
    }

    MiniString FormatNumber(double x) {
        MiniVector<double> one;
        one.push_back(x);
        return FormatReal(x, ChooseRealFormat(one));
    }

    // --- Elements ---

    static MiniString QuoteString(const MiniString& s) {
        MiniString r = "\"";
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '"') r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else if (c == '\t') r += "\\t";
            else if (c == '\r') r += "\\r";
            else r.push_back(c);
        }
        r += "\"";
        return r;
    }

    static MiniString FactorLabel(RValuePtr f, int i) {
        int code = f->i_vec[i];
        RValuePtr lev = f->attributes.count("levels") ? f->attributes["levels"] : nullptr;
        if (code == R_INT_NA || !lev || code < 1 || code > lev->Length()) return "<NA>";
        return AsStringAt(lev, code - 1);
    }

    // Every element as print() shows it, before padding. With quote = false (data frames),
    // strings are bare and a character NA shows as <NA>.
    static MiniVector<MiniString> FormatElements(RValuePtr v, bool quote) {
        MiniVector<MiniString> out;
        int n = v->Length();
        if (v->type == RType::INTEGER && HasClass(v, "factor")) {
            for (int i = 0; i < n; ++i) out.push_back(FactorLabel(v, i));
        } else if (v->type == RType::DOUBLE) {
            RealFormat f = ChooseRealFormat(v->d_vec);
            for (int i = 0; i < n; ++i) out.push_back(FormatReal(v->d_vec[i], f));
        } else if (v->type == RType::INTEGER) {
            for (int i = 0; i < n; ++i)
                out.push_back(v->i_vec[i] == R_INT_NA ? MiniString("NA") : MiniToString(v->i_vec[i]));
        } else if (v->type == RType::LOGICAL) {
            for (int i = 0; i < n; ++i) {
                int x = v->i_vec[i];
                out.push_back(x == R_LOGICAL_NA ? "NA" : (x ? "TRUE" : "FALSE"));
            }
        } else if (v->type == RType::CHARACTER) {
            for (int i = 0; i < n; ++i) {
                const MiniString& s = v->s_vec[i];
                if (IsNAString(s)) out.push_back(quote ? "NA" : "<NA>");
                else out.push_back(quote ? QuoteString(s) : s);
            }
        }
        return out;
    }

    static int MaxWidth(const MiniVector<MiniString>& cells) {
        int w = 0;
        for (auto& c : cells) w = std::max(w, (int)c.size());
        return w;
    }

    // --- Layouts ---

    // "[1] a b c" lines, wrapped at the print width with right-aligned [index] labels.
    static MiniString FormatIndexedLines(const MiniVector<MiniString>& cells, bool left_align) {
        int n = (int)cells.size();
        int w = MaxWidth(cells);
        int label_w = (int)MiniString("[" + MiniToString(n) + "]").size();
        int per_line = std::max(1, (kPrintWidth - label_w) / (w + 1));
        MiniString s;
        for (int start = 0; start < n; start += per_line) {
            if (start) s += "\n";
            s += PadLeft("[" + MiniToString(start + 1) + "]", label_w);
            for (int i = start; i < n && i < start + per_line; ++i) {
                s += " ";
                s += left_align ? PadRight(cells[i], w) : PadLeft(cells[i], w);
            }
        }
        return s;
    }

    // Named vectors: a row of names above a row of values, all right-aligned.
    static MiniString FormatNamedLines(const MiniVector<MiniString>& cells, RValuePtr names) {
        int n = (int)cells.size();
        MiniVector<MiniString> labels;
        int w = MaxWidth(cells);
        for (int i = 0; i < n; ++i) {
            MiniString nm = i < names->Length() ? AsStringAt(names, i) : MiniString("");
            if (IsNAString(nm)) nm = "<NA>";
            labels.push_back(nm);
            w = std::max(w, (int)nm.size());
        }
        int per_line = std::max(1, kPrintWidth / (w + 1));
        MiniString s;
        for (int start = 0; start < n; start += per_line) {
            if (start) s += "\n";
            MiniString top, bottom;
            for (int i = start; i < n && i < start + per_line; ++i) {
                top += PadLeft(labels[i], w) + " ";
                bottom += PadLeft(cells[i], w) + " ";
            }
            s += top + "\n" + bottom;
        }
        return s;
    }

    MiniString FormatVector(RValuePtr v) {
        bool is_factor = v->type == RType::INTEGER && HasClass(v, "factor");
        if (is_factor) {
            MiniString s = v->Length() == 0 ? MiniString("factor(0)") : FormatIndexedLines(FormatElements(v, false), true);
            s += "\nLevels:";
            if (v->attributes.count("levels")) {
                RValuePtr lev = v->attributes["levels"];
                for (int i = 0; i < lev->Length(); ++i) s += " " + AsStringAt(lev, i);
            }
            return s;
        }
        if (v->Length() == 0) {
            if (v->type == RType::DOUBLE) return "numeric(0)";
            if (v->type == RType::INTEGER) return "integer(0)";
            if (v->type == RType::LOGICAL) return "logical(0)";
            if (v->type == RType::CHARACTER) return "character(0)";
        }
        MiniVector<MiniString> cells = FormatElements(v, true);
        if (v->attributes.count("names") && v->attributes["names"]->Length() > 0) {
            MiniString s = FormatNamedLines(cells, v->attributes["names"]);
            // A 1-D table prints its (empty) dimnames header line first, as R does
            return HasClass(v, "table") ? "\n" + s : s;
        }
        return FormatIndexedLines(cells, v->type == RType::CHARACTER);
    }

    // Matrices: each column gets its own number layout; [r,] / [,c] labels.
    MiniString FormatMatrix(RValuePtr v, int nr, int nc) {
        bool left_align = v->type == RType::CHARACTER;
        MiniVector<MiniVector<MiniString>> cols;
        MiniVector<int> widths;
        for (int c = 0; c < nc; ++c) {
            auto col = std::make_shared<RValue>(v->type);
            for (int r = 0; r < nr; ++r) {
                int flat = c * nr + r;
                if (v->type == RType::DOUBLE) col->d_vec.push_back(v->d_vec[flat]);
                else if (v->type == RType::CHARACTER) col->s_vec.push_back(v->s_vec[flat]);
                else col->i_vec.push_back(v->i_vec[flat]);
            }
            MiniVector<MiniString> cells = FormatElements(col, true);
            MiniString header = "[," + MiniToString(c + 1) + "]";
            widths.push_back(std::max((int)header.size(), MaxWidth(cells)));
            cols.push_back(cells);
        }
        int label_w = (int)MiniString("[" + MiniToString(nr) + ",]").size();
        MiniString s = Spaces(label_w);
        for (int c = 0; c < nc; ++c) {
            MiniString header = "[," + MiniToString(c + 1) + "]";
            s += " ";
            s += left_align ? PadRight(header, widths[c]) : PadLeft(header, widths[c]);
        }
        for (int r = 0; r < nr; ++r) {
            s += "\n";
            s += PadLeft("[" + MiniToString(r + 1) + ",]", label_w);
            for (int c = 0; c < nc; ++c) {
                s += " ";
                s += left_align ? PadRight(cols[c][r], widths[c]) : PadLeft(cols[c][r], widths[c]);
            }
        }
        return s;
    }

    // Data frames: unquoted, right-aligned columns under their names; row names on the left.
    MiniString FormatDataFrame(RValuePtr v) {
        int ncol = (int)v->l_vec.size();
        int nrow = ncol > 0 ? v->l_vec[0]->Length() : 0;
        RValuePtr names = v->attributes.count("names") ? v->attributes["names"] : nullptr;
        auto col_name = [&](int j) -> MiniString {
            if (names && j < names->Length()) return AsStringAt(names, j);
            return "V" + MiniToString(j + 1);
        };

        if (ncol == 0) return "data frame with 0 columns and " + MiniToString(nrow) + " rows";
        if (nrow == 0) {
            MiniString s = "[1]";
            for (int j = 0; j < ncol; ++j) s += " " + col_name(j);
            return s + "\n<0 rows> (or 0-length row.names)";
        }

        MiniVector<MiniString> row_names;
        RValuePtr rn = v->attributes.count("row.names") ? v->attributes["row.names"] : nullptr;
        for (int i = 0; i < nrow; ++i)
            row_names.push_back(rn && i < rn->Length() ? AsStringAt(rn, i) : MiniToString(i + 1));
        int rn_w = MaxWidth(row_names);

        MiniVector<MiniVector<MiniString>> cols;
        MiniVector<int> widths;
        for (int j = 0; j < ncol; ++j) {
            MiniVector<MiniString> cells = FormatElements(v->l_vec[j], false);
            while ((int)cells.size() < nrow) cells.push_back("NA");
            widths.push_back(std::max((int)col_name(j).size(), MaxWidth(cells)));
            cols.push_back(cells);
        }

        MiniString s = Spaces(rn_w);
        for (int j = 0; j < ncol; ++j) s += " " + PadLeft(col_name(j), widths[j]);
        for (int i = 0; i < nrow; ++i) {
            s += "\n" + PadRight(row_names[i], rn_w);
            for (int j = 0; j < ncol; ++j) s += " " + PadLeft(cols[j][i], widths[j]);
        }
        return s;
    }

    // Lists: each element under a $name or [[i]] tag, separated by blank lines.
    MiniString FormatList(RValuePtr v) {
        int n = v->Length();
        if (n == 0) return "list()";
        RValuePtr names = v->attributes.count("names") ? v->attributes["names"] : nullptr;
        MiniString s;
        for (int i = 0; i < n; ++i) {
            MiniString nm = (names && i < names->Length()) ? AsStringAt(names, i) : MiniString("");
            if (!nm.empty() && !IsNAString(nm)) s += "$" + nm;
            else s += "[[" + MiniToString(i + 1) + "]]";
            s += "\n" + ToString(v->l_vec[i]) + "\n";
            if (i + 1 < n) s += "\n";
        }
        return s;
    }

} // namespace Evaluator
