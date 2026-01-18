#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace code_assistance {

enum class PlanStatus { DRAFT, REVIEW_REQUIRED, APPROVED, IN_PROGRESS, COMPLETED };

struct PlanStep {
    std::string id;
    std::string description;
    std::string files_touched;
    bool completed = false;
};

struct ExecutionPlan {
    std::string id;
    std::string goal;
    std::vector<PlanStep> steps;
    PlanStatus status = PlanStatus::DRAFT;
    int current_step_index = 0;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["goal"] = goal;
        j["status"] = (status == PlanStatus::APPROVED || status == PlanStatus::IN_PROGRESS) ? "APPROVED" : "REVIEW_REQUIRED";
        j["current_step"] = current_step_index;
        j["steps"] = nlohmann::json::array();
        for(const auto& s : steps) {
            j["steps"].push_back({
                {"id", s.id},
                {"description", s.description},
                {"files", s.files_touched},
                {"completed", s.completed}
            });
        }
        return j;
    }
};

class PlanningEngine {
public:
    ExecutionPlan current_plan;

    bool has_active_plan() const {
        return !current_plan.id.empty() && current_plan.status != PlanStatus::COMPLETED;
    }

    bool is_plan_approved() const {
        return current_plan.status == PlanStatus::APPROVED || current_plan.status == PlanStatus::IN_PROGRESS;
    }

    // Called by the Agent when it wants to propose a plan
    void propose_plan(const std::string& goal, const std::vector<nlohmann::json>& raw_steps) {
        current_plan.id = "PLAN_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        current_plan.goal = goal;
        current_plan.steps.clear();
        current_plan.status = PlanStatus::REVIEW_REQUIRED;
        current_plan.current_step_index = 0;

        for(const auto& j : raw_steps) {
            current_plan.steps.push_back({
                std::to_string(current_plan.steps.size() + 1),
                j.value("description", ""),
                j.value("files", ""),
                false
            });
        }
        spdlog::info("📝 New Plan Proposed: {} steps for '{}'", current_plan.steps.size(), goal);
    }

    // Called when User clicks "Approve" in UI
    void approve_plan() {
        current_plan.status = PlanStatus::APPROVED;
        spdlog::info("✅ Plan Approved by User. Execution unlocking.");
    }

    void mark_step_complete(int index) {
        if (index >= 0 && index < current_plan.steps.size()) {
            current_plan.steps[index].completed = true;
            current_plan.current_step_index = index + 1;
            
            if (current_plan.current_step_index >= current_plan.steps.size()) {
                current_plan.status = PlanStatus::COMPLETED;
            } else {
                current_plan.status = PlanStatus::IN_PROGRESS;
            }
        }
    }

    std::string get_plan_context_for_ai() {
        if (!has_active_plan()) return "";
        
        std::stringstream ss;
        ss << "### 📋 ACTIVE EXECUTION PLAN (Strictly Follow)\n";
        ss << "GOAL: " << current_plan.goal << "\n";
        
        for (size_t i = 0; i < current_plan.steps.size(); ++i) {
            const auto& s = current_plan.steps[i];
            ss << (i + 1) << ". [" << (s.completed ? "x" : " ") << "] " << s.description 
               << " (Target: " << s.files_touched << ")\n";
        }
        
        if (!is_plan_approved()) {
            ss << "\n⚠️ STATUS: PENDING USER APPROVAL. Do NOT execute code yet. Ask user to review.\n";
        } else {
            ss << "\n✅ STATUS: APPROVED. Execute Step " << (current_plan.current_step_index + 1) << " now.\n";
        }
        
        return ss.str();
    }
};

}