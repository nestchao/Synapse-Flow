#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace code_assistance {

enum class PlanStatus { DRAFT, REVIEW_REQUIRED, APPROVED, IN_PROGRESS, COMPLETED, FAILED };
enum class StepStatus { PENDING, APPROVED, IN_PROGRESS, SUCCESS, FAILED };

struct PlanStep {
    std::string id;
    std::string description;
    std::string tool_name;
    nlohmann::json params;
    StepStatus status = StepStatus::PENDING;
    std::string result_summary;

    // ✅ REQUIRED: JSON Serialization for API Response
    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"description", description},
            {"tool", tool_name},
            {"params", params},
            {"status", status == StepStatus::SUCCESS ? "SUCCESS" : "PENDING"}, 
            {"result", result_summary}
        };
    }
};

struct ExecutionPlan {
    std::string id;
    std::string goal;
    std::vector<PlanStep> steps;
    PlanStatus status = PlanStatus::DRAFT;
    size_t current_step_idx = 0;

    // ✅ REQUIRED: JSON Serialization for API Response
    nlohmann::json to_json() const {
        nlohmann::json steps_json = nlohmann::json::array();
        for(const auto& s : steps) steps_json.push_back(s.to_json());
        
        return {
            {"id", id},
            {"goal", goal},
            {"status", status == PlanStatus::APPROVED ? "APPROVED" : "REVIEW_REQUIRED"},
            {"current_step", current_step_idx},
            {"steps", steps_json}
        };
    }
};

class PlanningEngine {
private:
    ExecutionPlan current_plan_;
    std::mutex plan_mutex_;

public:
    void propose_plan(const std::string& goal, const std::vector<nlohmann::json>& raw_steps) {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        
        current_plan_ = ExecutionPlan();
        current_plan_.id = "PLAN_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        current_plan_.goal = goal;
        current_plan_.status = PlanStatus::REVIEW_REQUIRED;

        int idx = 1;
        for (const auto& j : raw_steps) {
            PlanStep step;
            step.id = std::to_string(idx++);
            step.description = j.value("description", "Unknown Step");
            step.tool_name = j.value("tool", "unknown");
            step.params = j.value("parameters", nlohmann::json::object());
            current_plan_.steps.push_back(step);
        }
        spdlog::info("📝 PlanningEngine: Proposed plan with {} steps. Waiting for User Approval.", current_plan_.steps.size());
    }

    void approve_plan() {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        if (current_plan_.status == PlanStatus::REVIEW_REQUIRED) {
            current_plan_.status = PlanStatus::APPROVED;
            for(auto& step : current_plan_.steps) step.status = StepStatus::APPROVED;
            spdlog::info("✅ PlanningEngine: Plan APPROVED by User.");
        }
    }

    // Helper: Check if plan exists
    bool has_active_plan() {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        return !current_plan_.id.empty() && 
               current_plan_.status != PlanStatus::COMPLETED && 
               current_plan_.status != PlanStatus::FAILED;
    }

    // Helper: Check if approved
    bool is_plan_approved() {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        return current_plan_.status == PlanStatus::APPROVED || 
               current_plan_.status == PlanStatus::IN_PROGRESS;
    }

    ExecutionPlan get_snapshot() {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        return current_plan_;
    }

    void mark_step_status(size_t index, StepStatus status, const std::string& result) {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        if (index < current_plan_.steps.size()) {
            current_plan_.steps[index].status = status;
            current_plan_.steps[index].result_summary = result;
            
            if (status == StepStatus::SUCCESS) {
                current_plan_.current_step_idx++;
                if (current_plan_.current_step_idx >= current_plan_.steps.size()) {
                    current_plan_.status = PlanStatus::COMPLETED;
                } else {
                    current_plan_.status = PlanStatus::IN_PROGRESS;
                }
            }
        }
    }

    std::string get_plan_context_for_ai() {
        std::lock_guard<std::mutex> lock(plan_mutex_);
        if (current_plan_.status == PlanStatus::DRAFT) return "";

        std::stringstream ss;
        ss << "\n### 📋 CURRENT EXECUTION PLAN\n";
        ss << "Status: " << (current_plan_.status == PlanStatus::APPROVED || current_plan_.status == PlanStatus::IN_PROGRESS ? "APPROVED (Execute now)" : "PENDING REVIEW (Do not execute)") << "\n";
        
        for (size_t i = 0; i < current_plan_.steps.size(); ++i) {
            const auto& s = current_plan_.steps[i];
            ss << (i + 1) << ". " << (i == current_plan_.current_step_idx ? "👉 " : "   ");
            ss << "[" << s.tool_name << "] " << s.description;
            if (s.status == StepStatus::SUCCESS) ss << " (DONE)";
            ss << "\n";
        }
        
        if (current_plan_.status != PlanStatus::APPROVED && current_plan_.status != PlanStatus::IN_PROGRESS) {
            ss << "\n⚠️ CONSTRAINT: You must ask the user to approve this plan before running any side-effect tools (edit, run).\n";
        } else {
            ss << "\n✅ AUTHORIZATION: You are authorized to execute step " << (current_plan_.current_step_idx + 1) << ".\n";
        }

        return ss.str();
    }
};

}