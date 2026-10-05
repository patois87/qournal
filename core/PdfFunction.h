/*
 * Qournal
 *
 * The functions of PDF files (section 7.10 of the specification): they give the colours of gradients and of
 * colour spaces like Separation and DeviceN. Understood are all four types: sampled (0), exponential (2),
 * stitching (3) and PostScript calculator (4).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QtGlobal>
#include <memory>
#include <vector>

#include "PdfFile.h"

namespace Pdf {

class Function {
public:
    virtual ~Function() = default;

    /// The outputs for the inputs, which are clipped to the domain first; the outputs are clipped to the range
    std::vector<double> operator()(std::vector<double> in) const;

    int inputs() const { return static_cast<int>(m_domain.size() / 2); }
    /// The number of outputs; 0 if it is only known after evaluating (type 4 without a range never has that)
    int outputs() const { return static_cast<int>(m_range.size() / 2); }

    /**
     * The places within the domain of a function of one input where it may jump: the bounds of stitching
     * functions. A gradient needs colours on both sides of them
     */
    virtual void jumps(std::vector<double>& places, int depth = 0) const {
        Q_UNUSED(places)
        Q_UNUSED(depth)
    }

    /// The function of a value (a dictionary or a stream), nullptr if it is not understood
    static std::shared_ptr<Function> parse(const Reader& reader, const Value& value, int depth = 0);

protected:
    virtual std::vector<double> evaluate(const std::vector<double>& in) const = 0;

    std::vector<double> m_domain;
    std::vector<double> m_range;  ///< empty if none is given (allowed for types 2 and 3)
};

}  // namespace Pdf
