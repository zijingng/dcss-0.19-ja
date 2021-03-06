/**
 * @file
 * database.cc
**/

#include "AppHdr.h"

#include "database.h"

#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef TARGET_COMPILER_VC
#include <unistd.h>
#endif

#include "clua.h"
#include "end.h"
#include "files.h"
#include "libutil.h"
#include "options.h"
#include "random.h"
#include "stringutil.h"
#include "syscalls.h"
#include "threads.h"
#include "unicode.h"

// TextDB handles dependency checking the db vs text files, creating the
// db, loading, and destroying the DB.
class TextDB
{
public:
    // db_name is the savedir-relative name of the db file,
    // minus the "db" extension.
    TextDB(const char* db_name, const char* dir, ...);
    TextDB(TextDB *parent);
    ~TextDB() { shutdown(true); delete translation; }
    void init();
    void shutdown(bool recursive = false);
    DBM* get() { return _db; }

    // Make it easier to migrate from raw DBM* to TextDB
    operator bool() const { return _db != 0; }
    operator DBM*() const { return _db; }

 private:
    bool _needs_update() const;
    void _regenerate_db();

 private:
    bool open_db();
    const char* const _db_name;
    string _directory;
    vector<string> _input_files;
    DBM* _db;
    string timestamp;
    TextDB *_parent;
    const char* lang() { return _parent ? Options.lang_name : 0; }
public:
    TextDB *translation;
};

// Convenience functions for (read-only) access to generic
// berkeley DB databases.
static void _store_text_db(const string &in, DBM *db);

static string _query_database(TextDB &db, string key, bool canonicalise_key,
                              bool run_lua, bool untranslated = false);
static void _add_entry(DBM *db, const string &k, string &v);

static TextDB AllDBs[] =
{
    TextDB("descriptions", "descript/",
            "features.txt",
            "items.txt",
            "unident.txt",
            "unrand.txt",
            "monsters.txt",
            "spells.txt",
            "gods.txt",
            "branches.txt",
            "skills.txt",
            "ability.txt",
            "cards.txt",
            "commands.txt",
            "clouds.txt",
            "status.txt",
            nullptr),

    TextDB("gamestart", "descript/",
            "species.txt",
            "backgrounds.txt",
            nullptr),

    TextDB("randart", "database/",
            "randname.txt",
            "rand_wpn.txt", // mostly weapons
            "rand_arm.txt", // mostly armour
            "rand_all.txt", // jewellery and general
            "randbook.txt", // artefact books
            // This doesn't really belong here, but they *are* god gifts...
            "monname.txt",  // orcish names for Beogh to choose from
            nullptr),

    TextDB("speak", "database/",
            "monspeak.txt", // monster speech
            "monspell.txt", // monster spellcasting speech
            "monflee.txt",  // monster fleeing speech
            "wpnnoise.txt", // noisy weapon speech
            "insult.txt",   // imp/demon taunts
            "godspeak.txt", // god speech
            nullptr),

    TextDB("shout", "database/",
            "shout.txt",
            "insult.txt",   // imp/demon taunts, again
            nullptr),

    TextDB("misc", "database/",
            "miscname.txt", // names for miscellaneous things
            "godname.txt",  // god-related names (mostly His Xomminess)
            "montitle.txt", // titles for monsters (i.e. uniques)
            nullptr),

    TextDB("quotes", "descript/",
            "quotes.txt",   // quotes for items and monsters
            nullptr),

    TextDB("help", "database/",
            "help.txt",     // database for outsourced help texts
            nullptr),

    TextDB("FAQ", "database/",
            "FAQ.txt",      // database for Frequently Asked Questions
            nullptr),

    TextDB("hints", "descript/",
            "hints.txt",    // hints mode
            "tutorial.txt", // tutorial mode
            nullptr),
    TextDB("jtrans", "database/zh/",
           "jtrans_ability.txt",
           "jtrans_abyss.txt",
           "jtrans_acquire.txt",
           "jtrans_actor.txt",
           "jtrans_adjust.txt",
           "jtrans_areas.txt",
           "jtrans_arena.txt",
           "jtrans_artefact.txt",
           "jtrans_art_func.txt",
           "jtrans_attack.txt",
           "jtrans_attitude_change.txt",
           "jtrans_beam.txt",
           "jtrans_behold.txt",
           "jtrans_branch.txt",
           "jtrans_butcher.txt",
           "jtrans_chardump.txt",
           "jtrans_cloud.txt",
           "jtrans_command.txt",
           "jtrans_dactions.txt",
           "jtrans_dbg_util.txt",
           "jtrans_decks.txt",
           "jtrans_delay.txt",
           "jtrans_describe_god.txt",
           "jtrans_describe_spells.txt",
           "jtrans_describe.txt",
           "jtrans_dgn_overview.txt",
           "jtrans_dgn_shoals.txt",
           "jtrans_directn.txt",
           "jtrans_dungeon.txt",
           "jtrans_end.txt",
           "jtrans_evoke.txt",
           "jtrans_exclude.txt",
           "jtrans_fearmonger.txt",
           "jtrans_fight.txt",
           "jtrans_files.txt",
           "jtrans_fineff.txt",
           "jtrans_food.txt",
           "jtrans_godabil.txt",
           "jtrans_godblessing.txt",
           "jtrans_godcompanions.txt",
           "jtrans_godconduct.txt",
           "jtrans_godpassive.txt",
           "jtrans_godprayer.txt",
           "jtrans_godwrath.txt",
           "jtrans_hints.txt",
           "jtrans_hiscores.txt",
           "jtrans_initfile.txt",
           "jtrans_invent.txt",
           "jtrans_item_use.txt",
           "jtrans_itemname.txt",
           "jtrans_itemprop.txt",
           "jtrans_items.txt",
           "jtrans_japanese.txt",
           "jtrans_kills.txt",
           "jtrans_lookup_help.txt",
           "jtrans_macro.txt",
           "jtrans_main.txt",
           "jtrans_makeitem.txt",
           "jtrans_melee_attack.txt",
           "jtrans_menu.txt",
           "jtrans_message.txt",
           "jtrans_misc.txt",
           "jtrans_mon_abil.txt",
           "jtrans_mon_act.txt",
           "jtrans_mon_behv.txt",
           "jtrans_mon_cast.txt",
           "jtrans_mon_clone.txt",
           "jtrans_mon_death.txt",
           "jtrans_mon_ench.txt",
           "jtrans_mon_grow.txt",
           "jtrans_mon_info.txt",
           "jtrans_mon_pathfind.txt",
           "jtrans_mon_place.txt",
           "jtrans_mon_poly.txt",
           "jtrans_mon_project.txt",
           "jtrans_mon_tentacle.txt",
           "jtrans_mon_util.txt",
           "jtrans_monster.txt",
           "jtrans_mutation.txt",
           "jtrans_mutant_beast.txt",
           "jtrans_near_danger.txt",
           "jtrans_newgame.txt",
           "jtrans_ng_input.txt",
           "jtrans_notes.txt",
           "jtrans_orb.txt",
           "jtrans_ouch.txt",
           "jtrans_output.txt",
           "jtrans_place.txt",
           "jtrans_player_act.txt",
           "jtrans_player_equip.txt",
           "jtrans_player_reacts.txt",
           "jtrans_player_stats.txt",
           "jtrans_player.txt",
           "jtrans_potion.txt",
           "jtrans_prompt.txt",
           "jtrans_quiver.txt",
           "jtrans_randbook.txt",
           "jtrans_ranged_attack.txt",
           "jtrans_religion.txt",
           "jtrans_rot.txt",
           "jtrans_shopping.txt",
           "jtrans_shout.txt",
           "jtrans_skill_menu.txt",
           "jtrans_skills.txt",
           "jtrans_species.txt",
           "jtrans_spl_book.txt",
           "jtrans_spl_cast.txt",
           "jtrans_spl_clouds.txt",
           "jtrans_spl_damage.txt",
           "jtrans_spl_goditem.txt",
           "jtrans_spl_miscast.txt",
           "jtrans_spl_monench.txt",
           "jtrans_spl_other.txt",
           "jtrans_spl_selfench.txt",
           "jtrans_spl_summoning.txt",
           "jtrans_spl_tornado.txt",
           "jtrans_spl_transloc.txt",
           "jtrans_spl_util.txt",
           "jtrans_spl_wpnench.txt",
           "jtrans_stairs.txt",
           "jtrans_startup.txt",
           "jtrans_stash.txt",
           "jtrans_state.txt",
           "jtrans_status.txt",
           "jtrans_target.txt",
           "jtrans_teleport.txt",
           "jtrans_terrain.txt",
           "jtrans_throw.txt",
           "jtrans_timed_effects.txt",
           "jtrans_transform.txt",
           "jtrans_traps.txt",
           "jtrans_travel.txt",
           "jtrans_tutorial.txt",
           "jtrans_view.txt",
           "jtrans_viewmap.txt",
           "jtrans_wiz_dgn.txt",
           "jtrans_wiz_fsim.txt",
           "jtrans_wiz_item.txt",
           "jtrans_wiz_mon.txt",
           "jtrans_wiz_you.txt",
           "jtrans_xom.txt",

           "jtrans_dat_des_altar.txt",
           "jtrans_dat_des_arrival.txt",
           "jtrans_dat_des_branches.txt",
           "jtrans_dat_des_builder.txt",
           "jtrans_dat_des_portals.txt",
           "jtrans_dat_des_serial.txt",
           "jtrans_dat_des_sprint.txt",
           "jtrans_dat_des_traps.txt",
           "jtrans_dat_des_tutorial.txt",
           "jtrans_dat_des_variable.txt",
           "jtrans_dat_clua.txt",
           "jtrans_dat_dlua.txt",

           "jtrans_skill_title.txt",
           "jtrans_divine_title.txt",
           "jtrans_weapon_name.txt",
           "jtrans_armour_name.txt",
           "jtrans_missile_name.txt",
           "jtrans_food_name.txt",
           "jtrans_wand_name.txt",
           "jtrans_potion_name.txt",
           "jtrans_scroll_name.txt",
           "jtrans_jewellery_name.txt",
           "jtrans_rune_name.txt",
           "jtrans_misc_name.txt",
           "jtrans_book_name.txt",
           "jtrans_staff_name.txt",
           "jtrans_rod_name.txt",
           "jtrans_god_name.txt",
           "jtrans_trap_name.txt",

           "jtrans_art_data.txt",
           "jtrans_branch_data.txt",
           "jtrans_duration_data.txt",
           "jtrans_feature_data.txt",
           "jtrans_form_data.txt",
           "jtrans_job_data.txt",
           "jtrans_monster_data.txt",
           "jtrans_mutation_data.txt",
           "jtrans_sacrifice_data.txt",
           "jtrans_species_data.txt",
           "jtrans_spell_data.txt",
           "jtrans_zap_data.txt",

           "montitle.txt",
           nullptr),

    TextDB("randart_vanilla", "database/zh/crawlj/",
           "randname.txt",
           "rand_wpn.txt",
           "rand_arm.txt",
           "rand_all.txt",
           nullptr),
};

static TextDB& DescriptionDB = AllDBs[0];
static TextDB& GameStartDB   = AllDBs[1];
static TextDB& RandartDB     = AllDBs[2];
static TextDB& SpeakDB       = AllDBs[3];
static TextDB& ShoutDB       = AllDBs[4];
static TextDB& MiscDB        = AllDBs[5];
static TextDB& QuotesDB      = AllDBs[6];
static TextDB& HelpDB        = AllDBs[7];
static TextDB& FAQDB         = AllDBs[8];
static TextDB& HintsDB       = AllDBs[9];
static TextDB& JtransDB      = AllDBs[10];
static TextDB& RandartVanillaDB = AllDBs[11];

static string _db_cache_path(string db, const char *lang)
{
    if (lang)
        db = db + "." + lang;
    return savedir_versioned_path("db/" + db);
}

// ----------------------------------------------------------------------
// TextDB
// ----------------------------------------------------------------------

TextDB::TextDB(const char* db_name, const char* dir, ...)
    : _db_name(db_name), _directory(dir),
      _db(nullptr), timestamp(""), _parent(0), translation(0)
{
    va_list args;
    va_start(args, dir);
    while (true)
    {
        const char* input_file = va_arg(args, const char *);

        if (input_file == 0)
            break;

        // probably forgot the terminating 0
        ASSERT(strstr(input_file, ".txt") != 0);
        _input_files.push_back(input_file);
    }
    va_end(args);
}

TextDB::TextDB(TextDB *parent)
    : _db_name(parent->_db_name),
      _directory(parent->_directory + Options.lang_name + "/"),
      _input_files(parent->_input_files), // FIXME: pointless copy
      _db(nullptr), timestamp(""), _parent(parent), translation(nullptr)
{
}

bool TextDB::open_db()
{
    if (_db)
        return true;

    const string full_db_path = _db_cache_path(_db_name, lang());
    _db = dbm_open(full_db_path.c_str(), O_RDONLY, 0660);
    if (!_db)
        return false;

    timestamp = _query_database(*this, "TIMESTAMP", false, false, true);
    if (timestamp.empty())
        return false;

    return true;
}

void TextDB::init()
{
    if (Options.lang_name && !_parent)
    {
        translation = new TextDB(this);
        translation->init();
    }

    open_db();

    if (!_needs_update())
        return;
    _regenerate_db();

    if (!open_db())
    {
        end(1, true, "Failed to open DB: %s",
            _db_cache_path(_db_name, lang()).c_str());
    }
}

void TextDB::shutdown(bool recursive)
{
    if (_db)
    {
        dbm_close(_db);
        _db = nullptr;
    }
    if (recursive && translation)
        translation->shutdown(recursive);
}

bool TextDB::_needs_update() const
{
    string ts;
    bool no_files = true;

    for (const string &file : _input_files)
    {
        string full_input_path = _directory + file;
        full_input_path = datafile_path(full_input_path, !_parent);
        time_t mtime = file_modtime(full_input_path);
#ifdef __ANDROID__
        if (file_exists(full_input_path))
#else
        if (mtime)
#endif
            no_files = false;
        char buf[20];
        snprintf(buf, sizeof(buf), ":%" PRId64, (int64_t)mtime);
        ts += buf;
    }

    if (no_files && timestamp.empty())
    {
        // No point in empty databases, although for simplicity keep ones
        // for disappeared translations for now.
        ASSERT(_parent);
        TextDB *en = _parent;
        delete en->translation; // ie, ourself
        en->translation = 0;
        return false;
    }

    return ts != timestamp;
}

void TextDB::_regenerate_db()
{
    shutdown();
#ifdef DEBUG_DIAGNOSTICS
    if (_parent)
        printf("Regenerating db: %s [%s]\n", _db_name, Options.lang_name);
    else
        printf("Regenerating db: %s\n", _db_name);
#endif

    string db_path = _db_cache_path(_db_name, lang());
    string full_db_path = db_path + ".db";

    {
        string output_dir = get_parent_directory(db_path);
        if (!check_mkdir("DB directory", &output_dir))
            end(1);
    }

    file_lock lock(db_path + ".lk", "wb");
#ifndef DGL_REWRITE_PROTECT_DB_FILES
    unlink_u(full_db_path.c_str());
#endif

    string ts;
    if (!(_db = dbm_open(db_path.c_str(), O_RDWR | O_CREAT, 0660)))
        end(1, true, "Unable to open DB: %s", db_path.c_str());
    for (const string &file : _input_files)
    {
        string full_input_path = _directory + file;
        full_input_path = datafile_path(full_input_path, !_parent);
        char buf[20];
        time_t mtime = file_modtime(full_input_path);
        snprintf(buf, sizeof(buf), ":%" PRId64, (int64_t)mtime);
        ts += buf;
        if (
#ifdef __ANDROID__
            file_exists(full_input_path)
#else
            mtime
#endif
            || !_parent) // english is mandatory
        {
            _store_text_db(full_input_path, _db);
        }
    }
    _add_entry(_db, "TIMESTAMP", ts);

    dbm_close(_db);
    _db = 0;
}

// ----------------------------------------------------------------------
// DB system
// ----------------------------------------------------------------------

#if !defined(DGAMELAUNCH) && !defined(TARGET_OS_WINDOWS)
static void* init_db(void *arg)
{
    AllDBs[(intptr_t)arg].init();
    return 0;
}
#endif

#define NUM_DB ARRAYSZ(AllDBs)

void databaseSystemInit()
{
    // Note: if you're building contrib libraries initially checked out
    // before 2011-12-28 and this assertion fails, please make sure you have
    // the current version ("git submodule sync;git submodule update --init").
    ASSERT(sqlite3_threadsafe());

    thread_t th[NUM_DB];
    for (unsigned int i = 0; i < NUM_DB; i++)
// Using threads for loading on Windows at the moment seems to cause
// random failures to find files (#5854); thus disabling it here until
// we can identify what's going on.
#if !defined(DGAMELAUNCH) && !defined(TARGET_OS_WINDOWS)
        if (thread_create_joinable(&th[i], init_db, (void*)(intptr_t)i))
#endif
        {
            // if thread creation fails, do it serially
            th[i] = 0;
            AllDBs[i].init();
        }
    for (unsigned int i = 0; i < NUM_DB; i++)
        if (th[i])
            thread_join(th[i]);
}

void databaseSystemShutdown()
{
    for (unsigned int i = 0; i < NUM_DB; i++)
        AllDBs[i].shutdown(true);
}

////////////////////////////////////////////////////////////////////////////
// Main DB functions

static datum _database_fetch(DBM *database, const string &key)
{
    datum result;
    result.dptr = nullptr;
    result.dsize = 0;
    datum dbKey;

    dbKey.dptr = (DPTR_COERCE) key.c_str();
    dbKey.dsize = key.length();

    // Don't use the database if called from "monster".
    if (database)
        result = dbm_fetch(database, dbKey);

    return result;
}

static vector<string> _database_find_keys(DBM *database,
                                          const string &regex,
                                          bool ignore_case,
                                          db_find_filter filter = nullptr)
{
    text_pattern             tpat(regex, ignore_case);
    vector<string> matches;

    datum dbKey = dbm_firstkey(database);

    while (dbKey.dptr != nullptr)
    {
        string key((const char *)dbKey.dptr, dbKey.dsize);

        if (tpat.matches(key)
            && key.find("__") == string::npos
            && (filter == nullptr || !(*filter)(key, "")))
        {
            matches.push_back(key);
        }

        dbKey = dbm_nextkey(database);
    }

    return matches;
}

static vector<string> _database_find_bodies(DBM *database,
                                            const string &regex,
                                            bool ignore_case,
                                            db_find_filter filter = nullptr)
{
    text_pattern             tpat(regex, ignore_case);
    vector<string> matches;

    datum dbKey = dbm_firstkey(database);

    while (dbKey.dptr != nullptr)
    {
        string key((const char *)dbKey.dptr, dbKey.dsize);

        datum dbBody = dbm_fetch(database, dbKey);
        string body((const char *)dbBody.dptr, dbBody.dsize);

        if (tpat.matches(body)
            && key.find("__") == string::npos
            && (filter == nullptr || !(*filter)(key, body)))
        {
            matches.push_back(key);
        }

        dbKey = dbm_nextkey(database);
    }

    return matches;
}

///////////////////////////////////////////////////////////////////////////
// Internal DB utility functions
static void _execute_embedded_lua(string &str)
{
    // Execute any lua code found between "{{" and "}}". The lua code
    // is expected to return a string, with which the lua code and
    // braces will be replaced.
    string::size_type pos = str.find("{{");
    while (pos != string::npos)
    {
        string::size_type end = str.find("}}", pos + 2);
        if (end == string::npos)
        {
            mprf(MSGCH_DIAGNOSTICS, "Unbalanced {{, bailing.");
            break;
        }

        string lua_full = str.substr(pos, end - pos + 2);
        string lua      = str.substr(pos + 2, end - pos - 2);

        if (clua.execstring(lua.c_str(), "db_embedded_lua", 1))
        {
            string err = "{{" + clua.error + "}}";
            str.replace(pos, lua_full.length(), err);
            return;
        }

        string result;
        clua.fnreturns(">s", &result);

        str.replace(pos, lua_full.length(), result);

        pos = str.find("{{", pos + result.length());
    }
}

static void _trim_leading_newlines(string &s)
{
    s.erase(0, s.find_first_not_of("\n"));
}

static void _add_entry(DBM *db, const string &k, string &v)
{
    _trim_leading_newlines(v);
    datum key, value;
    key.dptr = (char *) k.c_str();
    key.dsize = k.length();

    value.dptr = (char *) v.c_str();
    value.dsize = v.length();

    if (dbm_store(db, key, value, DBM_REPLACE))
        end(1, true, "Error storing %s", k.c_str());
}

static void _parse_text_db(LineInput &inf, DBM *db)
{
    string key;
    string value;

    bool in_entry = false;
    while (!inf.eof())
    {
        string line = inf.get_line();

        if (!line.empty() && line[0] == '#')
            continue;

        if (!line.compare(0, 4, "%%%%"))
        {
            if (!key.empty())
                _add_entry(db, key, value);
            key.clear();
            value.clear();
            in_entry = true;
            continue;
        }

        if (!in_entry)
            continue;

        if (key.empty())
        {
            key = line;
            trim_string(key);
            lowercase(key);
        }
        else
        {
            trim_string_right(line);
            value += line + "\n";
        }
    }

    if (!key.empty())
        _add_entry(db, key, value);
}

static void _store_text_db(const string &in, DBM *db)
{
    UTF8FileLineInput inf(in.c_str());
    if (inf.error())
        end(1, true, "Unable to open input file: %s", in.c_str());

    _parse_text_db(inf, db);
}

static string _chooseStrByWeight(string entry, int fixed_weight = -1)
{
    vector<string> parts;
    vector<int>    weights;

    vector<string> lines = split_string("\n", entry, false, true);

    int total_weight = 0;
    for (int i = 0, size = lines.size(); i < size; i++)
    {
        // Skip over multiple blank lines, and leading and trailing
        // blank lines.
        while (i < size && lines[i].empty())
            i++;

        if (i == size)
            break;

        int         weight;
        string part = "";

        if (sscanf(lines[i].c_str(), "w:%d", &weight))
        {
            i++;
            if (i == size)
                return "BUG, WEIGHT AT END OF ENTRY";
        }
        else
            weight = 10;

        total_weight += weight;

        while (i < size && !lines[i].empty())
        {
            part += lines[i++];
            part += "\n";
        }
        trim_string(part);

        parts.push_back(part);
        weights.push_back(total_weight);
    }

    if (parts.empty())
        return "BUG, EMPTY ENTRY";

    int choice = 0;
    if (fixed_weight != -1)
        choice = fixed_weight % total_weight;
    else
        choice = random2(total_weight);

    for (int i = 0, size = parts.size(); i < size; i++)
        if (choice < weights[i])
            return parts[i];

    return "BUG, NO STRING CHOSEN";
}

#define MAX_RECURSION_DEPTH 10
#define MAX_REPLACEMENTS    100

static string _getWeightedString(TextDB &db, const string &key,
                                 const string &suffix, int fixed_weight = -1)
{
    // We have to canonicalise the key (in case the user typed it
    // in and got the case wrong.)
    string canonical_key = key + suffix;
    lowercase(canonical_key);

    // Query the DB.
    datum result;

    if (db.translation)
        result = _database_fetch(db.translation->get(), canonical_key);
    if (result.dsize <= 0)
        result = _database_fetch(db.get(), canonical_key);

    if (result.dsize <= 0)
    {
        // Try ignoring the suffix.
        canonical_key = key;
        lowercase(canonical_key);

        // Query the DB.
        if (db.translation)
            result = _database_fetch(db.translation->get(), canonical_key);
        if (result.dsize <= 0)
            result = _database_fetch(db.get(), canonical_key);

        if (result.dsize <= 0)
            return "";
    }

    // Cons up a (C++) string to return. The caller must release it.
    string str = string((const char *)result.dptr, result.dsize);

    return _chooseStrByWeight(str, fixed_weight);
}

static void _call_recursive_replacement(string &str, TextDB &db,
                                        const string &suffix,
                                        int &num_replacements,
                                        int recursion_depth = 0);

static string _getRandomisedStr(TextDB &db, const string &key,
                                const string &suffix,
                                int &num_replacements,
                                int recursion_depth = 0)
{
    recursion_depth++;
    if (recursion_depth > MAX_RECURSION_DEPTH)
    {
        mprf(MSGCH_DIAGNOSTICS, "Too many nested replacements, bailing.");

        return "TOO MUCH RECURSION";
    }

    string str = _getWeightedString(db, key, suffix);

    _call_recursive_replacement(str, db, suffix, num_replacements,
                                recursion_depth);

    return str;
}

// Replace any "@foo@" markers that can be found in this database.
// Those that can't be found are left alone for the caller to deal with.
static void _call_recursive_replacement(string &str, TextDB &db,
                                        const string &suffix,
                                        int &num_replacements,
                                        int recursion_depth)
{
    string::size_type pos = str.find("@");
    while (pos != string::npos)
    {
        num_replacements++;
        if (num_replacements > MAX_REPLACEMENTS)
        {
            mprf(MSGCH_DIAGNOSTICS, "Too many string replacements, bailing.");
            return;
        }

        string::size_type end = str.find("@", pos + 1);
        if (end == string::npos)
        {
            mprf(MSGCH_DIAGNOSTICS, "Unbalanced @, bailing.");
            break;
        }

        string marker_full = str.substr(pos, end - pos + 1);
        string marker      = str.substr(pos + 1, end - pos - 1);

        string replacement =
            _getRandomisedStr(db, marker, suffix, num_replacements,
                              recursion_depth);

        if (replacement.empty())
        {
            // Nothing in database, leave it alone and go onto next @foo@
            pos = str.find("@", end + 1);
        }
        else
        {
            str.replace(pos, marker_full.length(), replacement);

            // Start search from pos rather than end + 1, so that if
            // the replacement contains its own @foo@, we can replace
            // that too.
            pos = str.find("@", pos);
        }
    } // while (pos != string::npos)
}

static string _query_database(TextDB &db, string key, bool canonicalise_key,
                              bool run_lua, bool untranslated)
{
    if (canonicalise_key)
    {
        // We have to canonicalise the key (in case the user typed it
        // in and got the case wrong.)
        lowercase(key);
    }

    // Query the DB.
    datum result;

    if (db.translation && !untranslated)
        result = _database_fetch(db.translation->get(), key);
    if (result.dsize <= 0)
        result = _database_fetch(db.get(), key);

    if (result.dsize <= 0)
        return "";

    string str((const char *)result.dptr, result.dsize);

    // <foo> is an alias to key foo
    if (str[0] == '<' && str[str.size() - 2] == '>'
        && str.find('<', 1) == str.npos
        && str.find('\n') == str.size() - 1)
    {
        return _query_database(db, str.substr(1, str.size() - 3),
                               canonicalise_key, run_lua, untranslated);
    }

    if (run_lua)
        _execute_embedded_lua(str);

    return str;
}

/////////////////////////////////////////////////////////////////////////////
// Quote DB specific functions.

string getQuoteString(const string &key)
{
    return unwrap_desc(_query_database(QuotesDB, key, true, true));
}

/////////////////////////////////////////////////////////////////////////////
// Description DB specific functions.

string getLongDescription(const string &key)
{
    return unwrap_desc(_query_database(DescriptionDB, key, true, true));
}

vector<string> getLongDescKeysByRegex(const string &regex,
                                      db_find_filter filter)
{
    if (!DescriptionDB.get())
    {
        vector<string> empty;
        return empty;
    }

    // FIXME: need to match regex against translated keys, which can't
    // be done by db only.
    return _database_find_keys(DescriptionDB.get(), regex, true, filter);
}

vector<string> getLongDescBodiesByRegex(const string &regex,
                                        db_find_filter filter)
{
    if (!DescriptionDB.get())
    {
        vector<string> empty;
        return empty;
    }

    // On partial translations, this will match only translated descriptions.
    // Not good, but otherwise we'd have to check hundreds of keys, with
    // two queries for each.
    // SQL can do this in one go, DBM can't.
    DBM *database = DescriptionDB.translation ?
        DescriptionDB.translation->get() : DescriptionDB.get();
    return _database_find_bodies(database, regex, true, filter);
}

/////////////////////////////////////////////////////////////////////////////
// GameStart DB specific functions.
string getGameStartDescription(const string &key)
{
    return _query_database(GameStartDB, key, true, true);
}

/////////////////////////////////////////////////////////////////////////////
// Shout DB specific functions.
string getShoutString(const string &monst, const string &suffix)
{
    int num_replacements = 0;

    return _getRandomisedStr(ShoutDB, monst, suffix, num_replacements);
}

/////////////////////////////////////////////////////////////////////////////
// Speak DB specific functions.
string getSpeakString(const string &key)
{
    int num_replacements = 0;

#ifdef DEBUG_MONSPEAK
    dprf(DIAG_SPEECH, "monster speech lookup for %s", key.c_str());
#endif
    string txt = _getRandomisedStr(SpeakDB, key, "", num_replacements);
    _execute_embedded_lua(txt);

    return txt;
}

/////////////////////////////////////////////////////////////////////////////
// Randname DB specific functions.
string getRandNameString(const string &itemtype, const string &suffix)
{
    int num_replacements = 0;

    return _getRandomisedStr(RandartDB, itemtype, suffix, num_replacements);
}

string getArteNameString(const string &itemtype, const string &suffix)
{
    int num_replacements = 0;

    if (Options.vanilla_randart_name)
        return _getRandomisedStr(RandartVanillaDB, itemtype, suffix, num_replacements);
    else
        return getRandNameString(itemtype, suffix);
}

/////////////////////////////////////////////////////////////////////////////
// Help DB specific functions.

string getHelpString(const string &topic)
{
    string help = _query_database(HelpDB, topic, false, true);
    if (help.empty())
        help = "Error! The help for \"" + topic + "\" is missing!";
    return help;
}

/////////////////////////////////////////////////////////////////////////////
// FAQ DB specific functions.
vector<string> getAllFAQKeys()
{
    if (!FAQDB.get())
    {
        vector<string> empty;
        return empty;
    }

    return _database_find_keys(FAQDB.get(), "^q.+", false);
}

string getFAQ_Question(const string &key)
{
    return _query_database(FAQDB, key, false, true);
}

string getFAQ_Answer(const string &question)
{
    string key = "a" + question.substr(1, question.length()-1);
    string val = unwrap_desc(_query_database(FAQDB, key, false, true));

    // Remove blank lines between items on a bulleted list, for small
    // terminals' sake. Far easier to store them as separated paragraphs
    // in the source.
    // Also, use a nicer bullet as we're already here.
    val = replace_all(val, "\n\n*", "\n•");

    return val;
}

/////////////////////////////////////////////////////////////////////////////
// Miscellaneous DB specific functions.

string getMiscString(const string &misc, const string &suffix)

{
    int num_replacements = 0;

    string txt = _getRandomisedStr(MiscDB, misc, suffix, num_replacements);
    _execute_embedded_lua(txt);

    return txt;
}

/////////////////////////////////////////////////////////////////////////////
// Hints DB specific functions.

string getHintString(const string &key)
{
    return unwrap_desc(_query_database(HintsDB, key, true, true));
}

/////////////////////////////////////////////////////////////////////////////
// Jtrans DB specific functions.

/**
 * jtrans(nullptr)しても例外が飛ばないようにするためのワンクッション
 *
 * @param key JtransDBに問い合わせたいキー文字列
 * @param linefeed trueなら語末に改行を付与する(デフォルトfalse)
 * @returns JtransDBのキー文字列keyに対応する文字列を返す
 */
string jtrans(const char* key, const bool linefeed)
{
    // for string(nullptr) error
    string str = (key == nullptr ? string() : string(key));

    return jtrans(str, linefeed);
}

/**
 * JtransDBオブジェクトからキー文字列keyに対応する文字列を引いて返す。
 * 文頭文末の改行・半角空白・タブは無視され、大文字小文字も区別されない。
 * key中の改行は"\n"に変換された上で辞書を引く。
 * 空文字のkeyには空文字を返す。
 * keyに対応する文字列が見つからなかった場合、keyを返す。
 * 対応する文字列が"__NONE"なら、空文字を返す。
 *
 * @param key JtransDBに問い合わせたいキー文字列
 * @param linefeed trueなら語末に改行を付与する(デフォルトfalse)
 * @returns JtransDBのキー文字列keyに対応する文字列を返す
 */
string jtrans(const string &key, const bool linefeed)
{
    if (key == "") return "";

    string tmp_key(key);

    tmp_key = trim_string(tmp_key);
    tmp_key = replace_all(tmp_key, "\n", "\\n");

    string text = _query_database(JtransDB, tmp_key, true, true);

    if (text == "") return key;
    if (trimmed_string(text) == "__NONE") return "";

    if (!linefeed)
    {
        string chomped_text(text.begin(), text.end()-1);
        return chomped_text;
    }

    return text;
}

/**
 * ある文字列keyがJtransDB内にキー文字列として存在するか否かを返す。
 * 空文字列が渡された場合はfalseとして扱う。
 *
 * @param key JtransDBに問い合わせたいキー文字列
 * @returns JtransDBのキー文字列keyに対応する文字列を返す
 */
bool jtrans_has_key(const string &key)
{
    if (key == "") return false;

    return (jtrans(key) != key);
}

/**
 * prefix + key + suffix(デフォルトで空文字列)がJtransDBにキーとして存在した
 * 場合、jtrans(prefix + key + suffix)を返す。
 * 存在しなかった場合、jtrans(key)を返す。(0.16-jaとの動作の差違に注意)
 *
 * @param prefix keyの前に付加される文字列 (例: "[title]", "wand of ")
 * @param key JtransDBに問い合わせたいキー文字列
 * @param suffix keyの後に付加される文字列 (例: " rune of Zot")
 * @returns JtransDBのキー文字列prefix + key + suffixに対応する文字列を返す
 */
string tagged_jtrans(const string &prefix, const string &key, const string &suffix)
{
    if (jtrans_has_key(prefix + trimmed_string(key) + suffix))
        return jtrans(prefix + trimmed_string(key) + suffix);
    else
        return jtrans(key);
}

/**
 * JtransDBオブジェクトからキー文字列keyに対応する文字列を引いて返す。
 * 文頭文末の改行・半角空白・タブは維持される。
 * 例:
 * jtrans("\nTest - ") -> "テスト -"
 * jtrans_notrim("\nTest - ") -> "\nテスト - "
 *
 * @param key JtransDBに問い合わせたいキー文字列
 * @returns JtransDBのキー文字列keyに対応する文字列を返す
 */
string jtrans_notrim(const string &key)
{
    string lhs = key.substr(0, key.find_first_not_of(" \t\n\r"));
    string rhs = key.substr(key.find_last_not_of(" \t\n\r") + 1);
    string val = jtrans(key);

    if(!val.empty())
        return lhs + jtrans(key) + rhs;
    else
        return "";
}
