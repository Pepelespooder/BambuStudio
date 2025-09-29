#include "SupportSpotsGenerator.hpp"

#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExtrusionProcessor.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrincipalComponents2D.hpp"
#include "Print.hpp"
#include "PrintBase.hpp"
#include "PrintConfig.hpp"
#include "Tesselate.hpp"
#include "libslic3r.h"
#include "tbb/parallel_for.h"
#include "tbb/blocked_range.h"
#include "tbb/blocked_range2d.h"
#include "tbb/parallel_reduce.h"
#include <algorithm>
#include <boost/log/trivial.hpp>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <utility>
#include <vector>

#include "AABBTreeLines.hpp"
#include "KDTreeIndirect.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "Geometry/ConvexHull.hpp"

// #define DETAILED_DEBUG_LOGS
// #define DEBUG_FILES

#ifdef DEBUG_FILES
#include <boost/nowide/cstdio.hpp>
#include "libslic3r/Color.hpp"
#endif


namespace Slic3r {

class ExtrusionLine
{
public:
    ExtrusionLine() : a(Vec2f::Zero()), b(Vec2f::Zero()), len(0.0), origin_entity(nullptr) {}
    ExtrusionLine(const Vec2f &a, const Vec2f &b, float len, const ExtrusionEntity *origin_entity)
        : a(a), b(b), len(len), origin_entity(origin_entity)
    {}

    ExtrusionLine(const Vec2f &a, const Vec2f &b)
        : a(a), b(b), len((a-b).norm()), origin_entity(nullptr)
    {}

    bool is_external_perimeter() const
    {
        assert(origin_entity != nullptr);
        return origin_entity->role() == erExternalPerimeter;
    }

    Vec2f                  a;
    Vec2f                  b;
    float                  len;
    const ExtrusionEntity *origin_entity;

    std::optional<SupportSpotsGenerator::SupportPointCause> support_point_generated = {};
    float form_quality            = 1.0f;
    float curled_up_height        = 0.0f;

    static const constexpr int Dim = 2;
    using Scalar                   = Vec2f::Scalar;
};

auto get_a(ExtrusionLine &&l) { return l.a; }
auto get_b(ExtrusionLine &&l) { return l.b; }

namespace SupportSpotsGenerator {

using LD = AABBTreeLines::LinesDistancer<ExtrusionLine>;

float get_flow_width(const LayerRegion *region, ExtrusionRole role)
{
    if (role == ExtrusionRole::erBridgeInfill) return region->flow(FlowRole::frExternalPerimeter).width();
    if (role == ExtrusionRole::erExternalPerimeter) return region->flow(FlowRole::frExternalPerimeter).width();
    if (role == ExtrusionRole::erGapFill) return region->flow(FlowRole::frInfill).width();
    if (role == ExtrusionRole::erPerimeter) return region->flow(FlowRole::frPerimeter).width();
    if (role == ExtrusionRole::erSolidInfill) return region->flow(FlowRole::frSolidInfill).width();
    if (role == ExtrusionRole::erInternalInfill) return region->flow(FlowRole::frInfill).width();
    if (role == ExtrusionRole::erTopSolidInfill) return region->flow(FlowRole::frTopSolidInfill).width();
    // default
    return region->flow(FlowRole::frPerimeter).width();
}

float estimate_curled_up_height(
    float distance, float curvature, float layer_height, float flow_width, float prev_line_curled_height, Params params)
{
    float curled_up_height = 0;
    if (fabs(distance) < 3.0 * flow_width) {
        curled_up_height = std::max(prev_line_curled_height - layer_height * 0.75f, 0.0f);
        //printf("If 1 %d\n",curled_up_height);
    }
    
    //printf("distance %f, params.malformation_distance_factors.first %f, params.malformation_distance_factors.second %f, flow_width %f\n", distance, params.malformation_distance_factors.first, params.malformation_distance_factors.second, flow_width);
    //printf("distance %f,params.malformation_distance_factors.first * flow_width %f, params.malformation_distance_factors.second * flow_width %f\n", distance, params.malformation_distance_factors.first * flow_width, params.malformation_distance_factors.second * flow_width);

    if (distance > params.malformation_distance_factors.first * flow_width &&
        distance < params.malformation_distance_factors.second * flow_width) {
        
        // imagine the extrusion profile. The part that has been glued (melted) with the previous layer will be called anchored section
        // and the rest will be called curling section
        // float anchored_section = flow_width - point.distance;
        float curling_section = distance;

        // after extruding, the curling (floating) part of the extrusion starts to shrink back to the rounded shape of the nozzle
        // The anchored part not, because the melted material holds to the previous layer well.
        // We can assume for simplicity perfect equalization of layer height and raising part width, from which:
        float swelling_radius = (layer_height + curling_section) / 2.0f;
        curled_up_height += std::max(0.f, (swelling_radius - layer_height) / 2.0f);

        // On convex turns, there is larger tension on the floating edge of the extrusion then on the middle section.
        // The tension is caused by the shrinking tendency of the filament, and on outer edge of convex trun, the expansion is greater and
        // thus shrinking force is greater. This tension will cause the curling section to curle up
        if (curvature > 0.01) {
            float radius    = (1.0 / curvature);
            float curling_t = sqrt(radius / 100);
            float b         = curling_t * flow_width;
            float a         = curling_section;
            float c         = sqrt(std::max(0.0f, a * a - b * b));

            curled_up_height += c;
        }
        curled_up_height = std::min(curled_up_height, params.max_curled_height_factor * layer_height);
    }

    return curled_up_height;
}

void estimate_malformations(LayerPtrs &layers, const Params &params)
{
	LD prev_layer_lines{};
	for (Layer *l : layers) {
        l->curled_lines.clear();
        std::vector<Linef> boundary_lines = l->lower_layer != nullptr ? to_unscaled_linesf(l->lower_layer->lslices) : std::vector<Linef>();
        AABBTreeLines::LinesDistancer<Linef> prev_layer_boundary{std::move(boundary_lines)};
        std::vector<ExtrusionLine>           current_layer_lines;
        for (const LayerRegion *layer_region : l->regions()) {
			for (const ExtrusionEntity *extrusion : layer_region->perimeters.flatten().entities) {
			    if (extrusion->role() != Slic3r::erExternalPerimeter)
                    continue;
				Points extrusion_pts;
                extrusion->collect_points(extrusion_pts);
				float flow_width       = get_flow_width(layer_region, extrusion->role());
				auto  annotated_points = estimate_points_properties<true, true, false, false>(extrusion_pts, prev_layer_lines, flow_width,
                                                                                             params.bridge_distance);
                for (size_t i = 0; i < annotated_points.size(); ++i) {
                	const ExtendedPoint &a = i > 0 ? annotated_points[i - 1] : annotated_points[i];
                    const ExtendedPoint &b = annotated_points[i];
                    ExtrusionLine line_out{a.position.cast<float>(), b.position.cast<float>(), float((a.position - b.position).norm()),
                                           extrusion};
                    Vec2f middle                               = 0.5 * (line_out.a + line_out.b);
                    auto [middle_distance, bottom_line_idx, x] = prev_layer_lines.distance_from_lines_extra<false>(middle);
                    ExtrusionLine bottom_line                  = prev_layer_lines.get_lines().empty() ? ExtrusionLine{} :
                                                                                                        prev_layer_lines.get_line(bottom_line_idx);

                    // correctify the distance sign using slice polygons
                    float sign = (prev_layer_boundary.distance_from_lines<true>(middle.cast<double>()) + 0.5f * flow_width) < 0.0f ? -1.0f : 1.0f;

                    line_out.curled_up_height = estimate_curled_up_height(middle_distance * sign, 0.5 * (a.curvature + b.curvature),
                                                                          l->height, flow_width, bottom_line.curled_up_height, params);

                    current_layer_lines.push_back(line_out);
                }
			}
        }
        for (const ExtrusionLine &line : current_layer_lines) {
            if (line.curled_up_height > params.curling_tolerance_limit) {
                l->curled_lines.push_back(CurledLine{Point::new_scale(line.a), Point::new_scale(line.b), line.curled_up_height});
            }
        }
        
        prev_layer_lines = LD{current_layer_lines};
    }
}

} // namespace SupportSpotsGenerator
} // namespace Slic3r