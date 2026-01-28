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
        if (params.contains("_batch_mode") && params["_batch_mode"].get<bool>()) {
            return {true, "Authorized (Batch Mode)"};
        }
        
        // 1. Allow "Safe" Read-Only tools anytime
        if (tool_name == "read_file" || tool_name == "list_dir" || 
            tool_name == "web_search" || tool_name == "pattern_search" || 
            tool_name == "propose_plan" || 
            tool_name == "FINAL_ANSWER") {
            return {true, "Safe tool allowed."};
        }

        // 2. Get Plan State
        auto plan = planner->get_snapshot();

        if (plan.status == PlanStatus::FAILED || plan.status == PlanStatus::COMPLETED) {
             if (tool_name == "FINAL_ANSWER") return {true, "Plan finished/failed, allowing explanation."};
             return {false, "BLOCKED: Plan is finished/failed. Use FINAL_ANSWER to close."};
        }

        if ((plan.status == PlanStatus::APPROVED || plan.status == PlanStatus::IN_PROGRESS) && 
            tool_name == "FINAL_ANSWER") {
            return {true, "Authorized: Agent declared mission complete."};
        }

        // 3. Check if Plan exists and is Approved
        if (!plan.id.empty() && plan.status != PlanStatus::APPROVED && plan.status != PlanStatus::IN_PROGRESS) {
            return {false, "BLOCKED: Plan exists but is not approved. Ask user for approval."};
        }

        // Check if Plan is missing (The case hitting you now)
        if (plan.id.empty()) {
            return {false, "BLOCKED: No active plan. You cannot use 'apply_edit' without a plan. Use 'propose_plan' first."};
        }

        // 4. Check if we are executing the expected step
        if (plan.current_step_idx >= plan.steps.size()) {
            return {false, "BLOCKED: Plan completed. No further actions authorized."};
        }

        const auto& current_step = plan.steps[plan.current_step_idx];

        // 5. Match Tool Name
        bool name_match = (current_step.tool_name == tool_name);
        
        if (!name_match) {
            // Fuzzy match for common synonyms
            if (current_step.tool_name.find(tool_name) != std::string::npos || 
                tool_name.find(current_step.tool_name) != std::string::npos) {
                name_match = true;
            }
            
            // Allow "apply_edit" if plan says "create_file" or "write_file" (common AI hallucinations)
            if (tool_name == "apply_edit" && 
               (current_step.tool_name == "create_file" || current_step.tool_name == "write_file")) {
                name_match = true;
            }
        }

        if (!name_match) {
            return {false, "DEVIATION DETECTED: Plan step " + current_step.id + 
                           " expects '" + current_step.tool_name + 
                           "', but Agent tried '" + tool_name + "'."};
        }

        // 6. Match Critical Parameters (e.g., File Paths)
        // This prevents the AI from saying "I'll edit file A" but editing "file B"
        if (tool_name == "apply_edit" || tool_name == "file_surgical_tool") {
            std::string planned_file = current_step.params.value("path", "");
            
            // ✅ FIX: Allow empty planned path if it was inferred as "unknown" originally
            // This prevents hard-locks if the AI forgot the path in the plan but provided it now.
            // BUT: We should log a warning.
            if (planned_file.empty()) {
                 spdlog::warn("⚠️ ExecutionGuard: Allowing action despite missing plan path (AI forgot to specify it in plan).");
                 return {true, "Allowed (Plan path was empty)."};
            }

            std::string actual_file = params.value("path", "");
            
            if (planned_file != actual_file) {
                return {false, "SECURITY ALERT: File path deviation. Planned: " + planned_file + ", Actual: " + actual_file};
            }
        }

        return {true, "Authorized by Plan Step " + current_step.id};
    }
};

}