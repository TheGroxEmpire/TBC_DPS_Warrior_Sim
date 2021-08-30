#include "sim_interface.hpp"

#include "Armory.hpp"
#include "Combat_simulator.hpp"
#include "Item_optimizer.hpp"
#include "Statistics.hpp"
#include "item_heuristics.hpp"

#define TEST_VIA_CONFIG

#include <sstream>

static const double q95 = Statistics::find_cdf_quantile(0.95, 0.01);

#ifdef TEST_VIA_CONFIG
void print_results(const Combat_simulator& sim, bool print_uptimes_and_procs)
{
    auto dd = sim.get_damage_distribution();

    auto f = 1.0 / (sim.config.sim_time * sim.config.n_batches);
    auto g = 60 * f;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "white (mh)    = " << f * dd.white_mh_damage << " (" << g * dd.white_mh_count << "x)" << std::endl;
    if (dd.white_oh_count > 0) std::cout << "white (oh)    = " << f * dd.white_oh_damage << " (" << g * dd.white_oh_count << "x)" << std::endl;
    if (dd.mortal_strike_count > 0) std::cout << "mortal strike = " << f * dd.mortal_strike_damage << " (" << g * dd.mortal_strike_count << "x)" << std::endl;
    if (dd.cleave_count > 0) std::cout << "cleave        = " << f * dd.cleave_damage << " (" << g * dd.cleave_count << "x)" << std::endl;
    if (dd.bloodthirst_count > 0) std::cout << "bloodthirst   = " << f * dd.bloodthirst_damage << " (" << g * dd.bloodthirst_count << "x)" << std::endl;
    if (dd.whirlwind_count > 0) std::cout << "whirlwind     = " << f * dd.whirlwind_damage << " (" << g * dd.whirlwind_count << "x)" << std::endl;
    if (dd.slam_count > 0) std::cout << "slam          = " << f * dd.slam_damage << " (" << g * dd.slam_count << "x)" << std::endl;
    if (dd.heroic_strike_count > 0) std::cout << "heroic strike = " << f * dd.heroic_strike_damage << " (" << g * dd.heroic_strike_count << "x)" << std::endl;
    if (dd.execute_count > 0) std::cout << "execute       = " << f * dd.execute_damage << " (" << g * dd.execute_count << "x)" << std::endl;
    if (dd.deep_wounds_count > 0) std::cout << "deep wounds   = " << f * dd.deep_wounds_damage << " (" << g * dd.deep_wounds_count << "x)" << std::endl;
    if (dd.overpower_count > 0) std::cout << "overpower     = " << f * dd.overpower_damage << " (" << g * dd.overpower_count << "x)" << std::endl;
    if (dd.item_hit_effects_count > 0) std::cout << "hit effects   = " << f * dd.item_hit_effects_damage << " (" << g * dd.item_hit_effects_count << "x)" << std::endl;
    std::cout << "----------------------" << std::endl;
    std::cout << "total         = " << f * dd.sum_damage_sources() << std::endl;
    std::cout << std::endl;

    if (print_uptimes_and_procs)
    {
        for (const auto& e : sim.get_aura_uptimes_map()) {
            std::cout << e.first << " " << 100 * f * e.second << "%" << std::endl;
        }
        std::cout << std::endl;
        for (const auto& e : sim.get_proc_data()) {
            std::cout << e.first << " " << g * e.second << " procs/min" << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << std::setprecision(6);
}
#endif

void compute_item_upgrade(const Character& character_new, const std::vector<int>& batches_per_iteration,
                          Combat_simulator& simulator, const Distribution& base_dps,
                          const std::string& item_name, const std::vector<size_t>& stronger_indexes,
                          size_t i, std::string& item_strengths_string, bool& found_upgrade,
                          std::string& item_downgrades_string)
{

    auto mean = 0.0;
    auto variance = 0.0;
    auto samples = 0;

    for (int batches : batches_per_iteration)
    {
        simulator.simulate(character_new, batches, Distribution(mean, variance, samples));

        const auto& d = simulator.get_dps_distribution();
        mean = d.mean();
        variance = d.variance();
        samples = d.samples();
        const auto std_of_the_mean = d.std_of_the_mean();

        if (mean - std_of_the_mean * q95 >= base_dps.mean() && samples > 5000)
        {
            found_upgrade = true;
            double dps_increase = mean - base_dps.mean();
            double dps_increase_std = std::sqrt(d.var_of_the_mean() + base_dps.var_of_the_mean());
            item_strengths_string += "<br><b>Up</b>grade: <b>" + item_name + "</b> ( +<b>" +
                                     String_helpers::string_with_precision(dps_increase, 2) + " &plusmn " +
                                     String_helpers::string_with_precision(dps_increase_std * q95, 2) + "</b> DPS).";
            break;
        }

        if (mean + std_of_the_mean * q95 <= base_dps.mean() && samples > 500)
        {
            double dps_decrease = mean - base_dps.mean();
            double dps_decrease_std = std::sqrt(d.var_of_the_mean() + base_dps.var_of_the_mean());
            item_downgrades_string += "<br><b>Down</b>grade: <b>" + item_name + "</b> ( <b>" +
                                      String_helpers::string_with_precision(dps_decrease, 3) + " &plusmn " +
                                      String_helpers::string_with_precision(dps_decrease_std * q95, 2) + "</b> DPS).";
            if (String_helpers::does_vector_contain(stronger_indexes, i))
            {
                found_upgrade = true;
                item_downgrades_string += " Note: Similar item stats, difficult to draw conclusions.";
            }
            break;
        }
    }
}

void item_upgrades(std::string& item_strengths_string, Character character_new, Item_optimizer& item_optimizer,
                   Armory& armory, const std::vector<int>& batches_per_iteration, Combat_simulator& simulator,
                   const Distribution& base_dps, Socket socket, const Special_stats& special_stats,
                   bool first_item)
{
    std::string dummy;
    std::string current{"Current "};
    auto current_armor = character_new.get_item_from_socket(socket, first_item);
    item_strengths_string += current;
    item_strengths_string =
        item_strengths_string + socket + ": <b>" + current_armor.name + "</b>";
    auto armor_vec = armory.get_items_in_socket(socket);
    auto items = (socket != Socket::trinket) ?
                     item_optimizer.remove_weaker_items(armor_vec, character_new.total_special_stats, dummy, 3) :
                     armor_vec;

    Armor item_in_socket = character_new.get_item_from_socket(socket, first_item);
    Special_stats item_special_stats = item_in_socket.special_stats;
    item_special_stats += item_in_socket.attributes.to_special_stats(special_stats);
    std::vector<size_t> stronger_indexes{};
    if (item_in_socket.set_name == Set::none && item_in_socket.use_effects.empty() &&
        item_in_socket.hit_effects.empty())
    {
        for (size_t i = 0; i < items.size(); i++)
        {
            if (items[i].set_name == Set::none && items[i].use_effects.empty() && items[i].hit_effects.empty())
            {
                Special_stats armor_special_stats = items[i].special_stats;
                armor_special_stats += items[i].attributes.to_special_stats(special_stats);

                if (estimate_special_stats_smart_no_skill(item_special_stats, armor_special_stats))
                {
                    stronger_indexes.emplace_back(i);
                }
            }
        }
    }

    std::string best_armor_name{};
    bool found_upgrade = false;
    std::string item_downgrades_string{};
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i].name == current_armor.name) continue;
        Armory::change_armor(character_new.armor, items[i], first_item);
        armory.compute_total_stats(character_new);
        compute_item_upgrade(character_new, batches_per_iteration, simulator, base_dps, items[i].name,
                             stronger_indexes, i, item_strengths_string, found_upgrade,
                             item_downgrades_string);
    }
    if (!found_upgrade)
    {
        item_strengths_string += " is <b>BiS</b> in current configuration!";
        item_strengths_string += item_downgrades_string;
        item_strengths_string += "<br><br>";
    }
    else
    {
        item_strengths_string += item_downgrades_string;
        item_strengths_string += "<br><br>";
    }
}

void maybe_equalize_weapon_specs(Character& c)
{
    const auto& mh = c.get_weapon_from_socket(Socket::main_hand);
    if (c.is_dual_wield())
    {
        const auto& oh = c.get_weapon_from_socket(Socket::off_hand);
        if (mh.type != oh.type) return;
    }
    switch (mh.type) {
    case Weapon_type::sword:
        c.talents.mace_specialization = c.talents.poleaxe_specialization = c.talents.sword_specialization;
        break;
    case Weapon_type::mace:
        c.talents.sword_specialization = c.talents.poleaxe_specialization = c.talents.mace_specialization;
        break;
    case Weapon_type::axe:
        c.talents.sword_specialization = c.talents.mace_specialization = c.talents.poleaxe_specialization;
        break;
    default:
        return;
    }
}

void item_upgrades_wep(std::string& item_strengths_string, Character character_new, Item_optimizer& item_optimizer,
                       Armory& armory, const std::vector<int>& batches_per_iteration, Combat_simulator& simulator,
                       const Distribution& base_dps, Weapon_socket weapon_socket)
{
    std::string dummy;
    Socket socket = ((weapon_socket == Weapon_socket::main_hand) || (weapon_socket == Weapon_socket::two_hand)) ?
                        Socket::main_hand :
                        Socket::off_hand;
    auto current_weapon = character_new.get_weapon_from_socket(socket);
    std::string current{"Current "};
    item_strengths_string += current;
    item_strengths_string =
        item_strengths_string + socket + ": " + "<b>" + current_weapon.name + "</b>";
    auto items = armory.get_weapon_in_socket(weapon_socket);
    items = item_optimizer.remove_weaker_weapons(weapon_socket, items, character_new.total_special_stats, dummy, 10);

    auto stronger_indexes = std::vector<size_t>{};

    std::string best_armor_name{};
    bool found_upgrade = false;
    std::string item_downgrades_string{};
    for (size_t i = 0, n = items.size(); i < n; ++i)
    {
        if (items[i].name == current_weapon.name) continue;
        Armory::change_weapon(character_new.weapons, items[i], socket);
        armory.compute_total_stats(character_new);
        compute_item_upgrade(character_new, batches_per_iteration, simulator, base_dps, items[i].name,
                             stronger_indexes, i, item_strengths_string, found_upgrade,
                             item_downgrades_string);
    }
    if (!found_upgrade)
    {
        item_strengths_string += " is <b>BiS</b> in current configuration!";
        item_strengths_string += item_downgrades_string;
        item_strengths_string += "<br><br>";
    }
    else
    {
        item_strengths_string += item_downgrades_string;
        item_strengths_string += "<br><br>";
    }
}

struct Stat_weight
{
    double mean;
    double std_of_the_mean;
    double amount;
};

Stat_weight compute_stat_weight(Combat_simulator& sim, Character& char_plus,
                                double permute_amount, double permute_factor,
                                const Distribution& base_dps)
{
    sim.simulate(char_plus);

    auto mean_diff = (sim.get_dps_mean() - base_dps.mean()) / permute_factor;
    auto std_of_the_mean_diff = std::sqrt(sim.get_var_of_the_mean() + base_dps.var_of_the_mean()) / permute_factor;

    return {mean_diff, q95 * std_of_the_mean_diff, permute_amount};
}

std::vector<double> get_damage_sources(const Damage_sources& damage_sources_vector)
{
    const auto total_damage = damage_sources_vector.sum_damage_sources();

    return {
        damage_sources_vector.white_mh_damage / total_damage,
        damage_sources_vector.white_oh_damage / total_damage,
        damage_sources_vector.bloodthirst_damage / total_damage,
        damage_sources_vector.execute_damage / total_damage,
        damage_sources_vector.heroic_strike_damage / total_damage,
        damage_sources_vector.cleave_damage / total_damage,
        damage_sources_vector.whirlwind_damage / total_damage,
        damage_sources_vector.hamstring_damage / total_damage,
        damage_sources_vector.deep_wounds_damage / total_damage,
        damage_sources_vector.item_hit_effects_damage / total_damage,
        damage_sources_vector.overpower_damage / total_damage,
        damage_sources_vector.slam_damage / total_damage,
        damage_sources_vector.mortal_strike_damage / total_damage,
        damage_sources_vector.sweeping_strikes_damage / total_damage,
    };
}

std::string print_stat(const std::string& stat_name, double amount)
{
    std::ostringstream stream;
    stream << stat_name << std::setprecision(4) << "<b>" << amount << "</b><br>";
    return stream.str();
}

std::string print_stat(const std::string& stat_name, double amount1, double amount2)
{
    std::ostringstream stream;
    stream << stat_name << std::setprecision(4) << "<b>" << amount1 << " &#8594 " << amount2 << "</b><br>";
    return stream.str();
}

std::string get_character_stat(const Character& char1, const Character& char2)
{
    std::string out_string = "<b>Setup 1 &#8594 Setup 2</b> <br>";
    out_string += print_stat("Strength: ", char1.total_attributes.strength, char2.total_attributes.strength);
    out_string += print_stat("Agility: ", char1.total_attributes.agility, char2.total_attributes.agility);
    out_string += print_stat("Hit: ", char1.total_special_stats.hit, char2.total_special_stats.hit);
    out_string += print_stat("Crit (spellbook):", char1.total_special_stats.critical_strike,
                             char2.total_special_stats.critical_strike);
    out_string +=
        print_stat("Attack Power: ", char1.total_special_stats.attack_power, char2.total_special_stats.attack_power);
    out_string +=
        print_stat("Haste factor: ", 1 + char1.total_special_stats.haste, 1 + char2.total_special_stats.haste);

    out_string += "<br><b>Armor:</b><br>";
    for (size_t i = 0; i < char1.armor.size(); i++)
    {
        if (char1.armor[i].name != char2.armor[i].name)
        {
            out_string += char1.armor[i].name + " &#8594 " + char2.armor[i].name + "<br>";
        }
    }

    out_string += "<br><b>Weapons:</b><br>";
    for (size_t i = 0; i < char1.weapons.size(); i++)
    {
        if (char1.weapons[i].name != char2.weapons[i].name)
        {
            if (i == 0)
            {
                out_string += "Mainhand: ";
            }
            else
            {
                out_string += "Offhand: ";
            }
            out_string += char1.weapons[i].name + " &#8594 " + char2.weapons[i].name + "<br><br>";
        }
    }

    out_string += "Set bonuses setup 1:<br>";
    for (const auto& bonus : char1.set_bonuses)
    {
        out_string += "<b>" + bonus.name + "-" + std::to_string(bonus.pieces) + "-pieces</b><br>";
    }

    out_string += "<br>Set bonuses setup 2:<br>";
    for (const auto& bonus : char1.set_bonuses)
    {
        out_string += "<b>" + bonus.name + "-" + std::to_string(bonus.pieces) + "-pieces</b><br>";
    }

    return out_string;
}

std::string get_character_stat(const Character& character)
{
    std::string out_string = "<b>Character stats:</b> <br />";
    out_string += print_stat("Strength: ", character.total_attributes.strength);
    out_string += print_stat("Agility: ", character.total_attributes.agility);
    out_string += print_stat("Hit: ", character.total_special_stats.hit);
    out_string += print_stat("Expertise (before rounding down): ", character.total_special_stats.expertise);
    out_string += print_stat("Crit (spellbook): ", character.total_special_stats.critical_strike);
    out_string += print_stat("Attack Power: ", character.total_special_stats.attack_power);
    out_string += print_stat("Haste factor: ", 1 + character.total_special_stats.haste);
    if (character.is_dual_wield())
    {
        if (character.has_weapon_of_type(Weapon_type::sword))
        {
            out_string += print_stat("Sword bonus expertise: ", character.total_special_stats.sword_expertise);
        }
        if (character.has_weapon_of_type(Weapon_type::axe))
        {
            out_string += print_stat("Axe bonus expertise: ", character.total_special_stats.axe_expertise);
        }
        if (character.has_weapon_of_type(Weapon_type::dagger))
        {
            out_string += print_stat("Dagger bonus expertise: ", 0);
        }
        if (character.has_weapon_of_type(Weapon_type::mace))
        {
            out_string += print_stat("Mace bonus expertise: ", character.total_special_stats.mace_expertise);
        }
        if (character.has_weapon_of_type(Weapon_type::unarmed))
        {
            out_string += print_stat("Unarmed bonus expertise: ", 0);
        }
    }
    else
    {
        if (character.has_weapon_of_type(Weapon_type::sword))
        {
            out_string += print_stat("Two Hand Sword expertise: ", character.total_special_stats.sword_expertise);
        }
        if (character.has_weapon_of_type(Weapon_type::axe))
        {
            out_string += print_stat("Two Hand Axe expertise: ", character.total_special_stats.axe_expertise);
        }
        if (character.has_weapon_of_type(Weapon_type::mace))
        {
            out_string += print_stat("Two Hand Mace expertise: ", character.total_special_stats.mace_expertise);
        }
    }

    out_string += "<br />";

    out_string += "Set bonuses:<br />";
    for (const auto& bonus : character.set_bonuses)
    {
        out_string += "<b>" + bonus.name + "-" + std::to_string(bonus.pieces) + "-pieces</b><br>";
    }

    return out_string;
}

std::string compute_talent_weight(Combat_simulator& combat_simulator, const Character& character,
                                  const Distribution& init_dps, const std::string& talent_name,
                                  int Character::talents_t::*talent, int n_points)
{
    auto without = init_dps;
    if (character.talents.*talent > 0)
    {
        auto copy = character;
        copy.talents.*talent = 0;
        combat_simulator.simulate(copy);
        without = combat_simulator.get_dps_distribution();
    }

    auto with = init_dps;
    if (character.talents.*talent < n_points)
    {
        auto copy = character;
        copy.talents.*talent = n_points;
        combat_simulator.simulate(copy);
        with = combat_simulator.get_dps_distribution();
    }

    auto mean_diff = (with.mean() - without.mean()) / n_points;
    auto std_of_the_mean_diff = std::sqrt(with.var_of_the_mean() + without.var_of_the_mean()) / n_points;

    return "<br>Talent: <b>" + talent_name + "</b><br>Value: <b>" +
           String_helpers::string_with_precision(mean_diff, 4) + " &plusmn " +
           String_helpers::string_with_precision(q95 * std_of_the_mean_diff, 3) + " DPS</b><br>";
}

std::string compute_talent_weights(Combat_simulator& sim, const Character& character, const Distribution& base_dps)
{
    const auto& config = sim.config;

    std::string talents_info = "<br><b>Value per 1 talent point:</b>";
    if (config.combat.use_heroic_strike)
    {
        if (config.number_of_extra_targets > 0 && config.combat.cleave_if_adds)
        {
            talents_info += compute_talent_weight(sim, character, base_dps, "Improved Cleave",
                                                  &Character::talents_t::improved_cleave, 3);
        }
        else
        {
            talents_info += compute_talent_weight(sim, character, base_dps, "Improved Heroic Strike",
                                                  &Character::talents_t::improved_heroic_strike, 3);
        }
    }

    if (config.combat.use_whirlwind)
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Improved Whirlwind",
                                              &Character::talents_t::improved_whirlwind, 2);
    }

    if (config.combat.use_slam && !character.is_dual_wield())
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Improved Slam",
                                              &Character::talents_t::improved_slam, 2);
    }

    if (config.combat.use_overpower)
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Improved Overpower",
                                              &Character::talents_t::improved_overpower, 2);
    }

    if (config.execute_phase_percentage_ > 0)
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Improved Execute",
                                              &Character::talents_t::improved_execute, 2);
    }

    if (character.is_dual_wield())
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Dual Wield Specialization",
                                              &Character::talents_t::dual_wield_specialization, 5);
    }

    if (character.is_dual_wield())
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "One-handed Weapon Specialization",
                                              &Character::talents_t::one_handed_weapon_specialization, 5);
    }

    if (!character.is_dual_wield())
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Two-handed Weapon Specialization",
                                              &Character::talents_t::two_handed_weapon_specialization, 5);
    }

    if (config.combat.use_death_wish)
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Death wish",
                                              &Character::talents_t::death_wish, 1);
    }

    if (character.has_weapon_of_type(Weapon_type::sword))
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Sword Specialization",
                                              &Character::talents_t::sword_specialization, 5);
    }

    if (character.has_weapon_of_type(Weapon_type::mace))
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Mace Specialization",
                                              &Character::talents_t::mace_specialization, 5);
    }

    if (character.has_weapon_of_type(Weapon_type::axe))
    {
        talents_info += compute_talent_weight(sim, character, base_dps, "Poleaxe Specialization",
                                              &Character::talents_t::poleaxe_specialization, 5);
    }

    talents_info += compute_talent_weight(sim, character, base_dps, "Flurry",
                                          &Character::talents_t::flurry, 5);

    talents_info += compute_talent_weight(sim, character, base_dps, "Cruelty",
                                          &Character::talents_t::cruelty, 5);

    talents_info += compute_talent_weight(sim, character, base_dps, "Impale",
                                          &Character::talents_t::impale, 2);

    talents_info += compute_talent_weight(sim, character, base_dps, "Rampage",
                                          &Character::talents_t::rampage, 1);

    talents_info += compute_talent_weight(sim, character, base_dps, "Weapon Mastery",
                                          &Character::talents_t::weapon_mastery, 2);

    talents_info += compute_talent_weight(sim, character, base_dps, "Precision",
                                          &Character::talents_t::precision, 3);

    talents_info += compute_talent_weight(sim, character, base_dps, "Improved Berserker Stance",
                                          &Character::talents_t::improved_berserker_stance, 5);

    talents_info += compute_talent_weight(sim, character, base_dps, "Unbridled Wrath",
                                          &Character::talents_t::unbridled_wrath, 5);

    talents_info += compute_talent_weight(sim, character, base_dps, "Anger Management",
                                          &Character::talents_t::anger_management, 1);

    talents_info += compute_talent_weight(sim, character, base_dps, "Endless Rage",
                                          &Character::talents_t::endless_rage, 1);

    return talents_info;
}

void compute_dpr(const Character& character, const Combat_simulator& simulator,
                 const Distribution& base_dps, const Damage_sources& dmg_dist, std::string& dpr_info)
{
    auto config = simulator.config;
    config.n_batches = 10000;

    dpr_info = "<br><b>Ability damage per rage:</b><br>";
    dpr_info += "DPR for ability X is computed as following:<br> "
                "((Normal DPS) - (DPS where ability X costs rage but has no effect)) / (rage cost of ability "
                "X)<br>";
    if (config.combat.use_bloodthirst)
    {
        double avg_bt_casts = static_cast<double>(dmg_dist.bloodthirst_count) / base_dps.samples();
        if (avg_bt_casts >= 1.0)
        {
            double bloodthirst_rage = 30 - 5 * character.has_set_bonus(Set::destroyer, 4);
            config.dpr_settings.compute_dpr_bt_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_hit = dmg_tot / avg_bt_casts;
            double dmg_per_rage = dmg_per_hit / bloodthirst_rage;
            dpr_info += "<b>Bloodthirst</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hit, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision(bloodthirst_rage, 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_bt_ = false;
        }
    }
    if (config.combat.use_mortal_strike)
    {
        double avg_ms_casts = static_cast<double>(dmg_dist.mortal_strike_count) / base_dps.samples();
        if (avg_ms_casts >= 1.0)
        {
            double mortal_strike_rage = 30 - 5 * character.has_set_bonus(Set::destroyer, 4);
            config.dpr_settings.compute_dpr_ms_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_hit = dmg_tot / avg_ms_casts;
            double dmg_per_rage = dmg_per_hit / mortal_strike_rage;
            dpr_info += "<b>Mortal Strike</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hit, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision(mortal_strike_rage, 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_ms_ = false;
        }
    }
    if (config.combat.use_whirlwind)
    {
        double avg_ww_casts = static_cast<double>(dmg_dist.whirlwind_count) / base_dps.samples();
        if (avg_ww_casts >= 1.0)
        {
            double whirlwind_rage = 25 - 5 * character.has_set_bonus(Set::warbringer, 2);
            config.dpr_settings.compute_dpr_ww_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_hit = dmg_tot / avg_ww_casts;
            double dmg_per_rage = dmg_per_hit / whirlwind_rage;
            dpr_info += "<b>Whirlwind</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hit, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision(whirlwind_rage, 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_ww_ = false;
        }
    }
    if (config.combat.use_slam)
    {
        double avg_sl_casts = static_cast<double>(dmg_dist.slam_count) / base_dps.samples();
        if (avg_sl_casts >= 1.0)
        {
            config.dpr_settings.compute_dpr_sl_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double avg_mh_dmg =
                static_cast<double>(dmg_dist.white_mh_damage) / static_cast<double>(dmg_dist.white_mh_count);
            double avg_mh_rage_lost = avg_mh_dmg * 3.75 / 274.7 + (3.5 * character.weapons[0].swing_speed / 2);
            double sl_cast_time = 1.5 - 0.5 * character.talents.improved_slam + config.combat.slam_latency;
            double dmg_per_hit = dmg_tot / avg_sl_casts;
            double dmg_per_rage = dmg_per_hit / (15.0 + avg_mh_rage_lost * sl_cast_time / character.weapons[0].swing_speed);
            dpr_info += "<b>Slam</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hit, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision(15.0 + avg_mh_rage_lost * sl_cast_time / character.weapons[0].swing_speed, 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_sl_ = false;
        }
    }
    if (config.combat.use_heroic_strike)
    {
        double avg_hs_casts = static_cast<double>(dmg_dist.heroic_strike_count) / base_dps.samples();
        if (avg_hs_casts >= 1.0)
        {
            double heroic_strike_rage = 15 - character.talents.improved_heroic_strike;
            config.dpr_settings.compute_dpr_hs_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_hs = dmg_tot / avg_hs_casts;
            double avg_mh_dmg =
                static_cast<double>(dmg_dist.white_mh_damage) / static_cast<double>(dmg_dist.white_mh_count);
            double avg_mh_rage_lost = avg_mh_dmg * 3.75 / 274.7 + (3.5 * character.weapons[0].swing_speed / 2);
            double dmg_per_rage = dmg_per_hs / (heroic_strike_rage + avg_mh_rage_lost);
            dpr_info += "<b>Heroic Strike</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hs, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision((heroic_strike_rage + avg_mh_rage_lost), 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_hs_ = false;
        }
    }
    if (config.combat.cleave_if_adds)
    {
        double avg_cl_casts = static_cast<double>(dmg_dist.cleave_count) / base_dps.samples();
        if (avg_cl_casts >= 1.0)
        {
            config.dpr_settings.compute_dpr_cl_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_hs = dmg_tot / avg_cl_casts;
            double avg_mh_dmg =
                static_cast<double>(dmg_dist.white_mh_damage) / static_cast<double>(dmg_dist.white_mh_count);
            double avg_mh_rage_lost = avg_mh_dmg * 3.75 / 274.7 + (3.5 * character.weapons[0].swing_speed / 2);
            double dmg_per_rage = dmg_per_hs / (20 + avg_mh_rage_lost);
            dpr_info += "<b>Cleave</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hs, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision((20 + avg_mh_rage_lost), 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_cl_ = false;
        }
    }
    if (config.combat.use_hamstring)
    {
        double avg_ha_casts = static_cast<double>(dmg_dist.hamstring_count) / base_dps.samples();
        if (avg_ha_casts >= 1.0)
        {
            config.dpr_settings.compute_dpr_ha_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_ha = dmg_tot / avg_ha_casts;
            double dmg_per_rage = dmg_per_ha / 10;
            dpr_info += "<b>Hamstring</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_ha, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision(10, 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_ha_ = false;
        }
    }
    if (config.combat.use_overpower)
    {
        double avg_op_casts = static_cast<double>(dmg_dist.overpower_count) / base_dps.samples();
        if (avg_op_casts >= 1.0)
        {
            config.dpr_settings.compute_dpr_op_ = true;
            Combat_simulator simulator_dpr{};
            simulator_dpr.set_config(config);
            simulator_dpr.simulate(character);
            double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
            double dmg_tot = delta_dps * config.sim_time;
            double dmg_per_hit = dmg_tot / avg_op_casts;
            double overpower_cost =
                simulator.get_rage_lost_stance() / double(base_dps.samples()) / avg_op_casts + 5.0;
            double dmg_per_rage = dmg_per_hit / overpower_cost;
            dpr_info += "<b>Overpower</b>: <br>Damage per cast: <b>" +
                        String_helpers::string_with_precision(dmg_per_hit, 4) + "</b><br>Average rage cost: <b>" +
                        String_helpers::string_with_precision(overpower_cost, 3) + "</b><br>DPR: <b>" +
                        String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
            config.dpr_settings.compute_dpr_op_ = false;
        }
    }

    double avg_ex_casts = static_cast<double>(dmg_dist.execute_count) / base_dps.samples();
    if (avg_ex_casts >= 1.0)
    {
        config.dpr_settings.compute_dpr_ex_ = true;
        Combat_simulator simulator_dpr{};
        simulator_dpr.set_config(config);
        simulator_dpr.simulate(character);
        double delta_dps = base_dps.mean() - simulator_dpr.get_dps_mean();
        double dmg_tot = delta_dps * config.sim_time;
        double dmg_per_hit = dmg_tot / avg_ex_casts;
        double execute_rage_cost = std::vector<int>{15, 13, 10}[character.talents.improved_execute];
        double execute_cost = simulator.get_avg_rage_spent_executing() / avg_ex_casts + execute_rage_cost;
        double dmg_per_rage = dmg_per_hit / execute_cost;
        dpr_info += "<b>Execute</b>: <br>Damage per cast: <b>" +
                    String_helpers::string_with_precision(dmg_per_hit, 4) + "</b><br>Average rage cost: <b>" +
                    String_helpers::string_with_precision(execute_cost, 3) + "</b><br>DPR: <b>" +
                    String_helpers::string_with_precision(dmg_per_rage, 4) + "</b><br>";
        config.dpr_settings.compute_dpr_ex_ = false;
    }
}

std::vector<std::string> compute_stat_weights(Combat_simulator& simulator, const Character& character, const Distribution& base_dps, const std::vector<std::string>& stat_weights)
{
    const auto rating_factor = 52.0 / 82;

    std::vector<std::string> sw_strings{};
    sw_strings.reserve(stat_weights.size());

    for (const auto& stat_weight : stat_weights)
    {
        Character char_plus = character;
        Stat_weight sw{};
        if (stat_weight == "strength")
        {
            char_plus.total_special_stats += Attributes{50, 0}.to_special_stats(char_plus.total_special_stats);
            sw = compute_stat_weight(simulator, char_plus, 10, 5, base_dps);
        }
        else if (stat_weight == "agility")
        {
            char_plus.total_special_stats += Attributes{0, 50}.to_special_stats(char_plus.total_special_stats);
            sw = compute_stat_weight(simulator, char_plus, 10, 5, base_dps);
        }
        else if (stat_weight == "ap")
        {
            char_plus.total_special_stats += {0, 0, 100};
            sw = compute_stat_weight(simulator, char_plus, 10, 10, base_dps);
        }
        else if (stat_weight == "crit")
        {
            char_plus.total_special_stats.critical_strike += rating_factor / 14 * 50;
            sw = compute_stat_weight(simulator, char_plus, 10, 5, base_dps);
        }
        else if (stat_weight == "hit")
        {
            char_plus.total_special_stats.hit += rating_factor / 10 * 25;
            sw = compute_stat_weight(simulator, char_plus, 10, 2.5, base_dps);
        }
        else if (stat_weight == "expertise")
        {
            char_plus.total_special_stats.expertise += rating_factor / 2.5 * 25;
            sw = compute_stat_weight(simulator, char_plus, 10, 2.5, base_dps);
        }
        else if (stat_weight == "haste")
        {
            char_plus.total_special_stats.haste += rating_factor / 10 * 0.01 * 50;
            sw = compute_stat_weight(simulator, char_plus, 10, 5, base_dps);
        }
        else if (stat_weight == "arpen")
        {
            char_plus.total_special_stats.gear_armor_pen += 350;
            sw = compute_stat_weight(simulator, char_plus, 10, 35, base_dps);
        }
        else
        {
            std::cout << "stat_weight '" << stat_weight << "' is not supported, continuing" << std::endl;
            continue;
        }
        sw_strings.emplace_back(stat_weight + ":" + std::to_string(sw.mean) + ":" + std::to_string(sw.std_of_the_mean));
    }
    return sw_strings;
}

Sim_output Sim_interface::simulate(const Sim_input& input)
{
    Armory armory{};

    auto temp_buffs = input.buffs;

    // Separate case for options which in reality are buffs. Add them to the buff list
    if (String_helpers::find_string(input.options, "mighty_rage_potion"))
    {
        temp_buffs.emplace_back("mighty_rage_potion");
    }
    else if (String_helpers::find_string(input.options, "haste_potion"))
    {
        temp_buffs.emplace_back("haste_potion");
    }
    else if (String_helpers::find_string(input.options, "insane_strength_potion"))
    {
        temp_buffs.emplace_back("insane_strength_potion");
    }
    else if (String_helpers::find_string(input.options, "heroic_potion"))
    {
        temp_buffs.emplace_back("heroic_potion");
    }
    if (String_helpers::find_string(input.options, "drums_of_battle"))
    {
        temp_buffs.emplace_back("drums_of_battle");
    }
    if (String_helpers::find_string(input.options, "bloodlust"))
    {
        temp_buffs.emplace_back("bloodlust");
    }
    if (String_helpers::find_string(input.options, "full_polarity"))
    {
        double full_polarity_val =
            String_helpers::find_value(input.float_options_string, input.float_options_val, "full_polarity_dd");
        armory.buffs.full_polarity.special_stats.damage_mod_physical = full_polarity_val / 100.0;
        armory.buffs.full_polarity.special_stats.damage_mod_spell = full_polarity_val / 100.0;
        temp_buffs.emplace_back("full_polarity");
    }
    if (String_helpers::find_string(input.options, "ferocious_inspiration"))
    {
        double ferocious_inspiration_val =
            String_helpers::find_value(input.float_options_string, input.float_options_val, "ferocious_inspiration_dd");
        armory.buffs.ferocious_inspiration.special_stats.damage_mod_physical = ferocious_inspiration_val / 100.0;
        armory.buffs.ferocious_inspiration.special_stats.damage_mod_spell = ferocious_inspiration_val / 100.0;
        temp_buffs.emplace_back("ferocious_inspiration");
    }
    if (String_helpers::find_string(input.options, "fungal_bloom"))
    {
        temp_buffs.emplace_back("fungal_bloom");
    }
    if (String_helpers::find_string(input.options, "battle_squawk"))
    {
        double battle_squawk_val =
            String_helpers::find_value(input.float_options_string, input.float_options_val, "battle_squawk_dd");
        armory.buffs.battle_squawk.special_stats.attack_speed = battle_squawk_val / 100.0;
        temp_buffs.emplace_back("battle_squawk");
    }

    const Character character = character_setup(armory, input.race[0], input.armor, input.weapons, temp_buffs,
                                                input.talent_string, input.talent_val, input.enchants, input.gems);

    // Simulator & Combat settings
    Combat_simulator_config config{input};
    Combat_simulator simulator{};
    simulator.set_config(config);

    std::vector<std::string> use_effects_schedule_string{};
    {
        auto use_effects_schedule = simulator.compute_use_effects_schedule(character);
        for (auto it = use_effects_schedule.crbegin(); it != use_effects_schedule.crend(); ++it)
        {
            use_effects_schedule_string.emplace_back(
                it->second.get().name + " " +
                String_helpers::string_with_precision(it->first, 3) + " " +
                String_helpers::string_with_precision(it->second.get().duration, 3));
        }
    }
    for (const auto& wep : character.weapons)
    {
        simulator.compute_hit_tables(character, character.total_special_stats, Weapon_sim(wep));
    }
    const bool is_dual_wield = character.is_dual_wield();
    const auto yellow_mh_ht = simulator.get_hit_probabilities_yellow_mh();
    const auto yellow_oh_ht = simulator.get_hit_probabilities_yellow_oh();
    const auto white_mh_ht = simulator.get_hit_probabilities_white_mh();
    const auto white_oh_ht = simulator.get_hit_probabilities_white_oh();
    const auto white_oh_ht_queued = simulator.get_hit_probabilities_white_oh_queued();

    simulator.simulate(character, true);
#ifdef TEST_VIA_CONFIG
    print_results(simulator, true);
#endif

    const auto base_dps = simulator.get_dps_distribution();
    std::vector<double> mean_dps_vec{base_dps.mean()};
    std::vector<double> sample_std_dps_vec{base_dps.std_of_the_mean()};

    const auto& hist_x = simulator.get_hist_x();
    const auto& hist_y = simulator.get_hist_y();

    const auto& dmg_dist = simulator.get_damage_distribution();
    const std::vector<double>& dps_dist_raw = get_damage_sources(dmg_dist);

    std::vector<std::string> aura_uptimes = simulator.get_aura_uptimes();
    std::vector<std::string> proc_statistics = simulator.get_proc_statistics();
    const auto& damage_time_lapse_raw = simulator.get_damage_time_lapse();
    std::vector<std::string> time_lapse_names;
    std::vector<std::vector<double>> damage_time_lapse;
    std::vector<double> dps_dist;
    std::vector<std::string> damage_names = {"White MH",      "White OH",         "Bloodthirst", "Execute",
                                             "Heroic Strike", "Cleave",           "Whirlwind",   "Hamstring",
                                             "Deep Wounds",   "Item Hit Effects", "Overpower",   "Slam",
                                             "Mortal Strike", "Sweeping Strikes", "Sword Specialization"};
    for (size_t i = 0; i < damage_time_lapse_raw.size(); i++)
    {
        double total_damage = 0;
        for (const auto& damage : damage_time_lapse_raw[i])
        {
            total_damage += damage;
        }
        if (total_damage > 0)
        {
            time_lapse_names.push_back(damage_names[i]);
            damage_time_lapse.push_back(damage_time_lapse_raw[i]);
            dps_dist.push_back(dps_dist_raw[i]);
        }
    }

    std::string character_stats = get_character_stat(character);

    // TODO(vigo) add rage gained or spent here, too
    std::string rage_info = "<b>Rage Statistics:</b><br>";
    rage_info += "(Average per simulation)<br>";
    rage_info += "Rage lost to rage cap (gaining rage when at 100): <b>" +
                 String_helpers::string_with_precision(simulator.get_rage_lost_capped() / base_dps.samples(), 3) + "</b><br>";
    rage_info += "</b>Rage lost when changing stance: <b>" +
                 String_helpers::string_with_precision(simulator.get_rage_lost_stance() / base_dps.samples(), 3) + "</b><br>";

    std::string extra_info_string = "<b>Fight stats vs. target:</b> <br/>";
    extra_info_string += "<b>Hit:</b> <br/>";
    extra_info_string += String_helpers::percent_to_str("Yellow hits", yellow_mh_ht.miss(), "chance to miss");
    extra_info_string += String_helpers::percent_to_str("Main-hand, white hits", white_mh_ht.miss(), "chance to miss");
    if (is_dual_wield)
    {
        extra_info_string += String_helpers::percent_to_str("Off-hand, white hits", white_oh_ht.miss(), "chance to miss");
        extra_info_string +=
            String_helpers::percent_to_str("Off-hand, while ability queued", white_oh_ht_queued.miss(), "chance to miss");
    }

    extra_info_string += "<b>Crit chance:</b> <br/>";
    extra_info_string += String_helpers::percent_to_str("Yellow main-hand", yellow_mh_ht.crit(), "chance to crit per cast");
    extra_info_string += String_helpers::percent_to_str("White main-hand", white_mh_ht.crit(), "chance to crit",
                                                        white_mh_ht.hit(), "left to crit-cap");

    if (is_dual_wield)
    {
        extra_info_string += String_helpers::percent_to_str("Yellow off-hand", yellow_oh_ht.crit(), "chance to crit per cast");
        extra_info_string += String_helpers::percent_to_str("White off-hand", white_oh_ht.crit(), "chance to crit",
                                                            white_oh_ht.hit(), "left to crit-cap");
    }
    extra_info_string += "<b>Glancing blows:</b><br/>";
    extra_info_string +=
        String_helpers::percent_to_str("Chance to occur", white_mh_ht.glance(), "(based on level difference)");
    extra_info_string +=
        String_helpers::percent_to_str("Glancing damage", 100 * white_mh_ht.glancing_penalty(), "(based on level difference)");
    extra_info_string += "<b>Other:</b><br/>";
    extra_info_string += String_helpers::percent_to_str("Main-hand dodge chance", yellow_mh_ht.dodge(), "(based on level difference and expertise)");
    if (is_dual_wield)
    {
        extra_info_string += String_helpers::percent_to_str("Off-hand dodge chance", yellow_oh_ht.dodge(), "(based on level difference and expertise)");
    }
    extra_info_string += "<br><br>";

    std::string dpr_info = "<br>(Hint: Ability damage per rage computations can be turned on under 'Simulation settings')";
    if (String_helpers::find_string(input.options, "compute_dpr"))
    {
        compute_dpr(character, simulator, base_dps, dmg_dist, dpr_info);
    }

    std::string talents_info = "<br>(Hint: Talent stat-weights can be activated under 'Simulation settings')";
    if (String_helpers::find_string(input.options, "talents_stat_weights"))
    {
        config.n_batches = static_cast<int>(String_helpers::find_value(input.float_options_string, input.float_options_val, "n_simulations_talent_dd"));
        Combat_simulator sim{};
        sim.set_config(config);
        talents_info = compute_talent_weights(sim, character, base_dps);
    }
#ifdef TEST_VIA_CONFIG
    if (talents_info.find("Value per 1 talent point") != std::string::npos)
    {
        for (size_t ppos = 0, pos = talents_info.find("<br>", ppos); pos != std::string::npos; ppos = pos + 4, pos = talents_info.find("<br>", ppos))
        {
            std::cout << talents_info.substr(ppos, pos - ppos) << std::endl;
        }
    }
    std::cout << std::endl;
#endif

    if (input.compare_armor.size() == 15 && input.compare_weapons.size() == 2)
    {
        Combat_simulator simulator_compare{};
        simulator_compare.set_config(config);
        Character character2 = character_setup(armory, input.race[0], input.compare_armor, input.compare_weapons,
                                               temp_buffs, input.talent_string, input.talent_val, input.enchants, input.gems);

        simulator_compare.simulate(character2);

        double mean_init_2 = simulator_compare.get_dps_mean();
        double sample_std_init_2 = simulator_compare.get_std_of_the_mean();

        character_stats = get_character_stat(character, character2);

        mean_dps_vec.push_back(mean_init_2);
        sample_std_dps_vec.push_back(sample_std_init_2);
    }

    std::string item_strengths_string;
    if (String_helpers::find_string(input.options, "item_strengths") || String_helpers::find_string(input.options, "wep_strengths"))
    {
        item_strengths_string = "<b>Character items and proposed upgrades:</b><br>";

        std::vector<int> batches_per_iteration = {100};
        for (int i = 0; i < 25; i++)
        {
            batches_per_iteration.push_back(static_cast<int>(batches_per_iteration.back() * 1.2));
        }

        Combat_simulator simulator_strength{};
        simulator_strength.set_config(config);
        Item_optimizer item_optimizer{};
        item_optimizer.race = get_race(input.race[0]);
        Character character_new = character_setup(armory, input.race[0], input.armor, input.weapons, temp_buffs,
                                                  input.talent_string, input.talent_val, input.enchants, input.gems);
        std::string dummy{};
        std::vector<Socket> all_sockets = {
            Socket::head, Socket::neck, Socket::shoulder, Socket::back, Socket::chest,   Socket::wrist,  Socket::hands,
            Socket::belt, Socket::legs, Socket::boots,    Socket::ring, Socket::trinket, Socket::ranged,
        };

        if (String_helpers::find_string(input.options, "item_strengths"))
        {
            for (auto socket : all_sockets)
            {
                if (socket == Socket::ring || socket == Socket::trinket)
                {
                    item_upgrades(item_strengths_string, character_new, item_optimizer, armory, batches_per_iteration,
                                  simulator_strength, base_dps, socket, character_new.total_special_stats, true);
                    item_upgrades(item_strengths_string, character_new, item_optimizer, armory, batches_per_iteration,
                                  simulator_strength, base_dps, socket, character_new.total_special_stats,
                                  false);
                }
                else
                {
                    item_upgrades(item_strengths_string, character_new, item_optimizer, armory, batches_per_iteration,
                                  simulator_strength, base_dps, socket, character_new.total_special_stats, true);
                }
            }
        }
        if (String_helpers::find_string(input.options, "wep_strengths"))
        {
            maybe_equalize_weapon_specs(character_new);

            if (is_dual_wield)
            {
                item_upgrades_wep(item_strengths_string, character_new, item_optimizer, armory, batches_per_iteration,
                                  simulator_strength, base_dps, Weapon_socket::main_hand);
                item_upgrades_wep(item_strengths_string, character_new, item_optimizer, armory, batches_per_iteration,
                                  simulator_strength, base_dps, Weapon_socket::off_hand);
            }
            else
            {
                item_upgrades_wep(item_strengths_string, character_new, item_optimizer, armory, batches_per_iteration,
                                  simulator_strength, base_dps, Weapon_socket::two_hand);
            }
        }
        item_strengths_string += "<br><br>";
    }

#ifdef TEST_VIA_CONFIG
    if (!item_strengths_string.empty())
    {
        for (size_t ppos = 0, pos = item_strengths_string.find("<br>", ppos); pos != std::string::npos; ppos = pos + 4, pos = item_strengths_string.find("<br>", ppos))
        {
            std::cout << item_strengths_string.substr(ppos, pos - ppos) << std::endl;
        }
    }
#endif

    std::vector<std::string> sw_strings{};
    if (!input.stat_weights.empty())
    {
        config.n_batches = static_cast<int>(String_helpers::find_value(input.float_options_string, input.float_options_val, "n_simulations_stat_dd"));
        Combat_simulator stat_weighs_simulator{};
        stat_weighs_simulator.set_config(config);

        sw_strings = compute_stat_weights(stat_weighs_simulator, character, base_dps, input.stat_weights);
    }

    std::string debug_topic{};
    if (String_helpers::find_string(input.options, "debug_on"))
    {
        config.display_combat_debug = true;

        simulator.set_config(config);
        double dps{};
        for (int i = 0; i < 100000; i++)
        {
            simulator.simulate(character);
            dps = simulator.get_dps_mean();
            if (std::abs(dps - base_dps.mean()) < 5)
            {
                break;
            }
        }
        debug_topic = simulator.get_debug_topic();

        debug_topic += "<br><br>";
        debug_topic += "Fight statistics:<br>";
        debug_topic += "DPS: " + std::to_string(dps) + "<br><br>";

        auto dist = simulator.get_damage_distribution();
        debug_topic += "DPS from sources:<br>";
        debug_topic += "DPS white MH: " + std::to_string(dist.white_mh_damage / config.sim_time) + "<br>";
        debug_topic += "DPS white OH: " + std::to_string(dist.white_oh_damage / config.sim_time) + "<br>";
        debug_topic += "DPS bloodthirst: " + std::to_string(dist.bloodthirst_damage / config.sim_time) + "<br>";
        debug_topic += "DPS mortal strike: " + std::to_string(dist.mortal_strike_damage / config.sim_time) + "<br>";
        debug_topic +=
            "DPS sweeping strikes: " + std::to_string(dist.sweeping_strikes_damage / config.sim_time) + "<br>";
        debug_topic += "DPS overpower: " + std::to_string(dist.overpower_damage / config.sim_time) + "<br>";
        debug_topic += "DPS slam: " + std::to_string(dist.slam_damage / config.sim_time) + "<br>";
        debug_topic += "DPS execute: " + std::to_string(dist.execute_damage / config.sim_time) + "<br>";
        debug_topic += "DPS heroic strike: " + std::to_string(dist.heroic_strike_damage / config.sim_time) + "<br>";
        debug_topic += "DPS cleave: " + std::to_string(dist.cleave_damage / config.sim_time) + "<br>";
        debug_topic += "DPS whirlwind: " + std::to_string(dist.whirlwind_damage / config.sim_time) + "<br>";
        debug_topic += "DPS hamstring: " + std::to_string(dist.hamstring_damage / config.sim_time) + "<br>";
        debug_topic += "DPS deep wounds: " + std::to_string(dist.deep_wounds_damage / config.sim_time) + "<br>";
        debug_topic +=
            "DPS item effects: " + std::to_string(dist.item_hit_effects_damage / config.sim_time) + "<br><br>";

        debug_topic += "Casts:<br>";
        debug_topic += "#Hits white MH: " + std::to_string(dist.white_mh_count) + "<br>";
        debug_topic += "#Hits white OH: " + std::to_string(dist.white_oh_count) + "<br>";
        debug_topic += "#Hits bloodthirst: " + std::to_string(dist.bloodthirst_count) + "<br>";
        debug_topic += "#Hits mortal strike: " + std::to_string(dist.mortal_strike_count) + "<br>";
        debug_topic += "#Hits sweeping strikes: " + std::to_string(dist.sweeping_strikes_count) + "<br>";
        debug_topic += "#Hits overpower: " + std::to_string(dist.overpower_count) + "<br>";
        debug_topic += "#Hits slam: " + std::to_string(dist.slam_count) + "<br>";
        debug_topic += "#Hits execute: " + std::to_string(dist.execute_count) + "<br>";
        debug_topic += "#Hits heroic strike: " + std::to_string(dist.heroic_strike_count) + "<br>";
        debug_topic += "#Hits cleave: " + std::to_string(dist.cleave_count) + "<br>";
        debug_topic += "#Hits whirlwind: " + std::to_string(dist.whirlwind_count) + "<br>";
        debug_topic += "#Hits hamstring: " + std::to_string(dist.hamstring_count) + "<br>";
        debug_topic += "#Hits deep_wounds: " + std::to_string(dist.deep_wounds_count) + "<br>";
        debug_topic += "#Hits item effects: " + std::to_string(dist.item_hit_effects_count) + "<br>";
    }

    for (auto& v : sample_std_dps_vec)
    {
        v *= q95;
    }

    return {hist_x,
            hist_y,
            dps_dist,
            time_lapse_names,
            damage_time_lapse,
            aura_uptimes,
            use_effects_schedule_string,
            proc_statistics,
            sw_strings,
            {item_strengths_string + extra_info_string + rage_info + dpr_info + talents_info, debug_topic},
            mean_dps_vec,
            sample_std_dps_vec,
            {character_stats}};
}
