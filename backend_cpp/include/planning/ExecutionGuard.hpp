#pragma once
#include "PlanningEngine.hpp"
#include <spdlog/spdlog.h>
#include <string>

namespace code_assistance {

struct GuardResult {
    bool allowed;
    std::string reason;
};

class ExecutionGuard {
public:
    static GuardResult validate_tool_call(
        const std::string& tool_name, 
        const nlohmann::json& params, 
        PlanningEngine* planner
    ) {
        // 1. Allow "Safe" Read-Only tools anytime
        if (tool_name == "read_file" || tool_name == "list_dir" || 
            tool_name == "web_search" || tool_name == "pattern_search" || 
            tool_name == "propose_plan") {
            return {true, "Safe tool allowed."};
        }

        // 2. Get Plan State
        auto plan = planner->get_snapshot();

        // 3. Check if Plan exists and is Approved
        if (plan.status != PlanStatus::APPROVED && plan.status != PlanStatus::IN_PROGRESS) {
            return {false, "BLOCKED: Active plan is not approved. Please review and approve the plan first."};
        }

        // 4. Check if we are executing the expected step
        if (plan.current_step_idx >= plan.steps.size()) {
            return {false, "BLOCKED: Plan completed. No further actions authorized."};
        }

        const auto& current_step = plan.steps[plan.current_step_idx];

        // 5. Match Tool Name
        if (current_step.tool_name != tool_name) {
            return {false, "DEVIATION DETECTED: Plan expects '" + current_step.tool_name + 
                           "', but Agent tried '" + tool_name + "'."};
        }

        // 6. Match Critical Parameters (e.g., File Paths)
        // This prevents the AI from saying "I'll edit file A" but editing "file B"
        if (tool_name == "apply_edit" || tool_name == "file_surgical_tool") {
            std::string planned_file = current_step.params.value("path", "");
            std::string actual_file = params.value("path", "");
            
            if (planned_file != actual_file) {
                return {false, "SECURITY ALERT: File path deviation. Planned: " + planned_file + ", Actual: " + actual_file};
            }
        }

        return {true, "Authorized by Plan Step " + current_step.id};
    }
};

}