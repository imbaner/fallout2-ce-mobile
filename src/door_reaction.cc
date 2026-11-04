#include "door_reaction.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <vector>

#include "interpreter.h"

namespace fallout {

namespace {

    // Layout of a compiled script (see `programCreateByPath`).
    constexpr size_t kProceduresOffset = 42;
    constexpr size_t kProcedureSize = 24;
    constexpr size_t kProcedureNameOffset = 0;
    constexpr size_t kProcedureFlagsOffset = 4;
    constexpr size_t kProcedureBodyOffset = 16;

    // A script that takes longer than that counts as a change of the door
    // (real ones take less than a thousand).
    constexpr int kMaxSteps = 40000;

    // Commands of the game (`interpreter_extra.cc`) the reader knows.
    constexpr int kOpSelfObj = 0x80BC;
    constexpr int kOpSourceObj = 0x80BD;
    constexpr int kOpTargetObj = 0x80BE;
    constexpr int kOpDudeObj = 0x80BF;
    constexpr int kOpMetarule3 = 0x80E1;
    constexpr int kOpMessageStr = 0x8105;
    constexpr int kOpMetarule = 0x810B;
    constexpr int kOpSetProtoData = 0x8205;

    constexpr int kMetaruleFirstRun = 14;
    constexpr int kMetaruleIsLoadGame = 22;
    constexpr int kMetaruleWeaponDamageType = 49;
    constexpr int kMetarule3TileGetNextCritter = 106;

    // `set_proto_data` offset of a weapon's damage type.
    constexpr int kProtoDataWeaponDamageType = 48;

    enum class CommandKind {
        // Takes its arguments and gives a value nobody knows in advance.
        Read,
        // Shows something, gives nothing.
        Message,
        // Acts on anything but the door, gives nothing.
        Side,
    };

    struct Command {
        int opcode;
        int arguments;
        CommandKind kind;
    };

    constexpr Command kCommands[] = {
        { 0x80A1, 1, CommandKind::Side }, // give_exp_points
        { 0x80A3, 1, CommandKind::Side }, // play_sfx
        { 0x80A4, 1, CommandKind::Read }, // obj_name
        { 0x80A6, 1, CommandKind::Read }, // get_pc_stat
        { 0x80A7, 3, CommandKind::Read }, // tile_contains_pid_obj
        { 0x80AA, 2, CommandKind::Read }, // has_skill
        { 0x80AC, 3, CommandKind::Read }, // roll_vs_skill
        { 0x80AE, 3, CommandKind::Read }, // do_check
        { 0x80AF, 1, CommandKind::Read }, // is_success
        { 0x80B0, 1, CommandKind::Read }, // is_critical
        { 0x80B4, 2, CommandKind::Read }, // random
        { 0x80B8, 1, CommandKind::Message }, // display_msg
        { 0x80B9, 0, CommandKind::Message }, // script_overrides
        { 0x80BA, 2, CommandKind::Read }, // obj_is_carrying_obj_pid
        { 0x80BB, 3, CommandKind::Read }, // tile_contains_obj_pid
        { 0x80C0, 0, CommandKind::Read }, // obj_being_used_with
        { 0x80C1, 1, CommandKind::Read }, // local_var
        { 0x80C2, 2, CommandKind::Side }, // set_local_var
        { 0x80C3, 1, CommandKind::Read }, // map_var
        { 0x80C4, 2, CommandKind::Side }, // set_map_var
        { 0x80C5, 1, CommandKind::Read }, // global_var
        { 0x80C6, 2, CommandKind::Side }, // set_global_var
        { 0x80C7, 0, CommandKind::Read }, // script_action
        { 0x80C8, 1, CommandKind::Read }, // obj_type
        { 0x80C9, 1, CommandKind::Read }, // obj_item_subtype
        { 0x80CA, 2, CommandKind::Read }, // get_critter_stat
        { 0x80CE, 3, CommandKind::Side }, // animate_move_obj_to_tile
        { 0x80D2, 2, CommandKind::Read }, // tile_distance
        { 0x80D3, 2, CommandKind::Read }, // tile_distance_objs
        { 0x80D4, 1, CommandKind::Read }, // tile_num
        { 0x80D5, 3, CommandKind::Read }, // tile_num_in_direction
        { 0x80DC, 2, CommandKind::Read }, // obj_can_see_obj
        { 0x80EA, 0, CommandKind::Read }, // game_time
        { 0x80EB, 0, CommandKind::Read }, // game_time_in_seconds
        { 0x80EC, 1, CommandKind::Read }, // elevation
        { 0x80F2, 1, CommandKind::Read }, // game_ticks
        { 0x80F3, 3, CommandKind::Read }, // has_trait
        { 0x80F7, 0, CommandKind::Read }, // fixed_param
        { 0x80F8, 1, CommandKind::Read }, // tile_is_visible
        { 0x80FA, 0, CommandKind::Read }, // action_being_used
        { 0x80FB, 1, CommandKind::Read }, // critter_state
        { 0x8100, 1, CommandKind::Read }, // obj_pid
        { 0x8101, 0, CommandKind::Read }, // cur_map_index
        { 0x8104, 2, CommandKind::Read }, // proto_data
        { 0x8106, 2, CommandKind::Read }, // critter_inven_obj
        { 0x810A, 3, CommandKind::Message }, // float_msg
        { 0x810E, 2, CommandKind::Side }, // reg_anim_func
        { 0x8118, 0, CommandKind::Read }, // get_month
        { 0x8119, 0, CommandKind::Read }, // get_day
        { 0x8123, 1, CommandKind::Read }, // get_poison
        { 0x8128, 0, CommandKind::Read }, // combat_is_initialized
        { 0x812A, 0, CommandKind::Read }, // game difficulty
        { 0x812D, 1, CommandKind::Read }, // obj_is_locked
        { 0x8130, 1, CommandKind::Read }, // obj_is_open
        { 0x8143, 2, CommandKind::Side }, // attack_setup
        { 0x8149, 1, CommandKind::Read }, // obj_art_fid
        { 0x814A, 1, CommandKind::Read }, // art_anim
        { 0x814B, 1, CommandKind::Read }, // party_member_obj
        { 0x814C, 2, CommandKind::Read }, // rotation_to_tile
        { 0x814F, 0, CommandKind::Read }, // combat difficulty
        { 0x8154, 1, CommandKind::Message }, // debug_msg
        { OPCODE_FETCH_EXTERNAL, 1, CommandKind::Read },
        { OPCODE_STORE_EXTERNAL, 2, CommandKind::Side },
    };

    enum class ValueKind {
        Number,
        Unknown,
        String,
        Dude,
        Self,
        Weapon,
        // The bottom of the return stack: the procedure is over.
        End,
    };

    struct Value {
        ValueKind kind;
        int number;
    };

    constexpr Value kUnknown = { ValueKind::Unknown, 0 };
    constexpr Value kString = { ValueKind::String, 0 };

    Value makeNumber(int number)
    {
        return { ValueKind::Number, number };
    }

    bool isObject(const Value& value)
    {
        return value.kind == ValueKind::Dude || value.kind == ValueKind::Self || value.kind == ValueKind::Weapon;
    }

    // True, false or nobody knows.
    enum class Truth {
        False,
        True,
        Unknown,
    };

    Truth truthOf(const Value& value)
    {
        if (value.kind == ValueKind::Number) {
            return value.number != 0 ? Truth::True : Truth::False;
        }
        return isObject(value) ? Truth::True : Truth::Unknown;
    }

    Value valueOf(Truth truth)
    {
        return truth == Truth::Unknown ? kUnknown : makeNumber(truth == Truth::True ? 1 : 0);
    }

    Truth truthOf(bool condition)
    {
        return condition ? Truth::True : Truth::False;
    }

    Truth equals(const Value& a, const Value& b)
    {
        if (a.kind == ValueKind::Number && b.kind == ValueKind::Number) {
            return truthOf(a.number == b.number);
        }
        if (isObject(a) && isObject(b)) {
            return truthOf(a.kind == b.kind);
        }
        if ((isObject(a) && b.kind == ValueKind::Number && b.number == 0)
            || (isObject(b) && a.kind == ValueKind::Number && a.number == 0)) {
            return Truth::False;
        }
        return Truth::Unknown;
    }

    // A compiled script, every read checked: a mod's file can be anything.
    class ScriptFile {
    public:
        ScriptFile(const unsigned char* data, size_t size)
            : data(data)
            , size(size)
        {
        }

        bool has(size_t position, size_t length) const
        {
            return position <= size && length <= size - position;
        }

        bool read16(size_t position, int* value) const
        {
            if (!has(position, 2)) {
                return false;
            }
            *value = (data[position] << 8) | data[position + 1];
            return true;
        }

        bool read32(size_t position, int* value) const
        {
            if (!has(position, 4)) {
                return false;
            }
            *value = static_cast<int>((static_cast<unsigned int>(data[position]) << 24)
                | (data[position + 1] << 16)
                | (data[position + 2] << 8)
                | data[position + 3]);
            return true;
        }

        int procedureCount() const
        {
            int count;
            return read32(kProceduresOffset, &count) && count >= 0 && has(kProceduresOffset + 4, static_cast<size_t>(count) * kProcedureSize)
                ? count
                : 0;
        }

        bool procedureField(int index, size_t field, int* value) const
        {
            return read32(kProceduresOffset + 4 + static_cast<size_t>(index) * kProcedureSize + field, value);
        }

        bool procedureNameIs(int index, const char* name) const
        {
            int nameOffset;
            if (!procedureField(index, kProcedureNameOffset, &nameOffset) || nameOffset < 0) {
                return false;
            }

            size_t identifiers = kProceduresOffset + 4 + static_cast<size_t>(procedureCount()) * kProcedureSize;
            size_t length = strlen(name);
            if (!has(identifiers + static_cast<size_t>(nameOffset), length + 1)) {
                return false;
            }

            const unsigned char* text = data + identifiers + nameOffset;
            for (size_t pos = 0; pos < length; pos++) {
                if (tolower(text[pos]) != name[pos]) {
                    return false;
                }
            }
            return text[length] == '\0';
        }

    private:
        const unsigned char* data;
        size_t size;
    };

    struct Path {
        int position;
        int framePointer;
        std::vector<Value> stack;
        std::vector<Value> returnStack;
        // Variables of the script set on the way (the rest is unknown).
        std::map<int, Value> variables;
    };

    class Reader {
    public:
        Reader(const ScriptFile& script, int damageType)
            : script(script)
            , damageType(damageType)
        {
        }

        DoorReaction read(int bodyOffset);

    private:
        enum class Step {
            Next,
            // The procedure is over, or this path was taken over by another.
            Done,
            // The door may change, nothing else to find out.
            DoorChanges,
        };

        Step step(Path& path);
        Step binary(Path& path, int opcode);
        Step metarule(Path& path);
        Step metarule3(Path& path);
        Step branch(Path& path, int opcode);
        static bool pop(Path& path, Value* value);
        static bool popNumber(Path& path, int* number);
        static bool hasVariable(const Path& path, int index);
        bool seenAlready(const Path& path);

        const ScriptFile& script;
        int damageType;
        std::vector<Path> waiting;
        std::vector<std::vector<int>> seen;
        DoorReaction reaction = DoorReaction::None;
    };

    bool Reader::pop(Path& path, Value* value)
    {
        if (path.stack.empty()) {
            return false;
        }
        *value = path.stack.back();
        path.stack.pop_back();
        return true;
    }

    bool Reader::popNumber(Path& path, int* number)
    {
        Value value;
        if (!pop(path, &value) || value.kind != ValueKind::Number) {
            return false;
        }
        *number = value.number;
        return true;
    }

    // A variable of the procedure, [index] from the frame.
    bool Reader::hasVariable(const Path& path, int index)
    {
        long long position = static_cast<long long>(path.framePointer) + index;
        return position >= 0 && position < static_cast<long long>(path.stack.size());
    }

    // A path that came to the same place with the same values goes on the
    // same way.
    bool Reader::seenAlready(const Path& path)
    {
        std::vector<int> key = { path.position, path.framePointer, static_cast<int>(path.stack.size()), static_cast<int>(path.returnStack.size()) };
        for (const std::vector<Value>* values : { &path.stack, &path.returnStack }) {
            for (const Value& value : *values) {
                key.push_back(static_cast<int>(value.kind));
                key.push_back(value.number);
            }
        }
        for (const auto& variable : path.variables) {
            key.push_back(variable.first);
            key.push_back(static_cast<int>(variable.second.kind));
            key.push_back(variable.second.number);
        }

        auto place = std::lower_bound(seen.begin(), seen.end(), key);
        if (place != seen.end() && *place == key) {
            return true;
        }
        seen.insert(place, std::move(key));
        return false;
    }

    DoorReaction Reader::read(int bodyOffset)
    {
        // As a call from the game: no arguments, nowhere to return.
        Path start = { bodyOffset, 0, { makeNumber(0) }, { { ValueKind::End, 0 } }, {} };
        waiting.push_back(std::move(start));

        int steps = 0;
        while (!waiting.empty()) {
            Path path = std::move(waiting.back());
            waiting.pop_back();
            if (seenAlready(path)) {
                continue;
            }

            Step result = Step::Next;
            while (result == Step::Next) {
                if (++steps > kMaxSteps) {
                    return DoorReaction::Door;
                }
                result = step(path);
            }

            if (result == Step::DoorChanges) {
                return DoorReaction::Door;
            }
        }

        return reaction;
    }

    Reader::Step Reader::binary(Path& path, int opcode)
    {
        Value b;
        Value a;
        if (!pop(path, &b) || !pop(path, &a)) {
            return Step::DoorChanges;
        }

        bool numbers = a.kind == ValueKind::Number && b.kind == ValueKind::Number;
        long long x = a.number;
        long long y = b.number;
        Value result = kUnknown;

        switch (opcode) {
        case OPCODE_EQUAL:
            result = valueOf(equals(a, b));
            break;
        case OPCODE_NOT_EQUAL: {
            Truth same = equals(a, b);
            result = valueOf(same == Truth::Unknown ? same : truthOf(same == Truth::False));
            break;
        }
        case OPCODE_LESS_THAN:
            result = numbers ? makeNumber(x < y) : kUnknown;
            break;
        case OPCODE_LESS_THAN_EQUAL:
            result = numbers ? makeNumber(x <= y) : kUnknown;
            break;
        case OPCODE_GREATER_THAN:
            result = numbers ? makeNumber(x > y) : kUnknown;
            break;
        case OPCODE_GREATER_THAN_EQUAL:
            result = numbers ? makeNumber(x >= y) : kUnknown;
            break;
        case OPCODE_ADD:
            if (numbers) {
                result = makeNumber(static_cast<int>(x + y));
            } else if (a.kind == ValueKind::String || b.kind == ValueKind::String) {
                result = kString;
            }
            break;
        case OPCODE_SUB:
            result = numbers ? makeNumber(static_cast<int>(x - y)) : kUnknown;
            break;
        case OPCODE_MUL:
            result = numbers ? makeNumber(static_cast<int>(x * y)) : kUnknown;
            break;
        case OPCODE_DIV:
            result = numbers && y != 0 ? makeNumber(static_cast<int>(x / y)) : kUnknown;
            break;
        case OPCODE_MOD:
            result = numbers && y != 0 ? makeNumber(static_cast<int>(x % y)) : kUnknown;
            break;
        case OPCODE_BITWISE_AND:
            result = numbers ? makeNumber(static_cast<int>(x & y)) : kUnknown;
            break;
        case OPCODE_BITWISE_OR:
            result = numbers ? makeNumber(static_cast<int>(x | y)) : kUnknown;
            break;
        case OPCODE_BITWISE_XOR:
            result = numbers ? makeNumber(static_cast<int>(x ^ y)) : kUnknown;
            break;
        case OPCODE_AND: {
            Truth p = truthOf(a);
            Truth q = truthOf(b);
            result = valueOf(p == Truth::False || q == Truth::False ? Truth::False
                    : p == Truth::True && q == Truth::True          ? Truth::True
                                                                    : Truth::Unknown);
            break;
        }
        case OPCODE_OR: {
            Truth p = truthOf(a);
            Truth q = truthOf(b);
            result = valueOf(p == Truth::True || q == Truth::True ? Truth::True
                    : p == Truth::False && q == Truth::False      ? Truth::False
                                                                  : Truth::Unknown);
            break;
        }
        }

        path.stack.push_back(result);
        return Step::Next;
    }

    Reader::Step Reader::metarule(Path& path)
    {
        Value argument;
        int rule;
        if (!pop(path, &argument) || !popNumber(path, &rule)) {
            return Step::DoorChanges;
        }

        switch (rule) {
        case kMetaruleWeaponDamageType:
            if (argument.kind == ValueKind::Weapon && damageType != kDoorReactionUnknownDamageType) {
                path.stack.push_back(makeNumber(damageType));
            } else if (argument.kind == ValueKind::Self) {
                // No weapon: "Not a weapon!", 0.
                path.stack.push_back(makeNumber(0));
            } else {
                path.stack.push_back(kUnknown);
            }
            return Step::Next;
        case kMetaruleFirstRun:
        case kMetaruleIsLoadGame:
            path.stack.push_back(kUnknown);
            return Step::Next;
        }

        return Step::DoorChanges;
    }

    Reader::Step Reader::metarule3(Path& path)
    {
        Value argument;
        for (int index = 0; index < 3; index++) {
            if (!pop(path, &argument)) {
                return Step::DoorChanges;
            }
        }

        int rule;
        if (!popNumber(path, &rule) || rule != kMetarule3TileGetNextCritter) {
            return Step::DoorChanges;
        }

        path.stack.push_back(kUnknown);
        return Step::Next;
    }

    Reader::Step Reader::branch(Path& path, int opcode)
    {
        Value condition;
        if (!pop(path, &condition) || path.stack.empty()) {
            return Step::DoorChanges;
        }

        Truth truth = truthOf(condition);
        if (truth == Truth::Unknown) {
            Path other = path;
            int address;
            if (!popNumber(other, &address)) {
                return Step::DoorChanges;
            }
            other.position = address;
            waiting.push_back(std::move(other));
            truth = Truth::True;
        }

        if (truth == Truth::True) {
            // `opWhile` leaves the address for the end of the loop.
            if (opcode == OPCODE_IF) {
                path.stack.pop_back();
            }
            return Step::Next;
        }

        return popNumber(path, &path.position) ? Step::Next : Step::DoorChanges;
    }

    Reader::Step Reader::step(Path& path)
    {
        int opcode;
        if (path.position < 0 || !script.read16(static_cast<size_t>(path.position), &opcode)) {
            return Step::DoorChanges;
        }
        path.position += 2;

        Value value;
        int number;

        switch (opcode) {
        case VALUE_TYPE_INT:
            if (!script.read32(static_cast<size_t>(path.position), &number)) {
                return Step::DoorChanges;
            }
            path.position += 4;
            path.stack.push_back(makeNumber(number));
            return Step::Next;
        case VALUE_TYPE_STRING:
        case VALUE_TYPE_FLOAT:
            path.position += 4;
            path.stack.push_back(opcode == VALUE_TYPE_STRING ? kString : kUnknown);
            return Step::Next;
        case OPCODE_NOOP:
        case OPCODE_ENTER_CRITICAL_SECTION:
        case OPCODE_LEAVE_CRITICAL_SECTION:
            return Step::Next;
        case OPCODE_PUSH_BASE:
            if (!popNumber(path, &number) || number < 0 || static_cast<size_t>(number) > path.stack.size()) {
                return Step::DoorChanges;
            }
            path.returnStack.push_back(makeNumber(path.framePointer));
            path.framePointer = static_cast<int>(path.stack.size()) - number;
            return Step::Next;
        case OPCODE_POP_BASE:
            if (path.returnStack.empty() || path.returnStack.back().kind != ValueKind::Number) {
                return Step::DoorChanges;
            }
            path.framePointer = path.returnStack.back().number;
            path.returnStack.pop_back();
            return Step::Next;
        case OPCODE_POP_TO_BASE:
            if (path.framePointer < 0 || static_cast<size_t>(path.framePointer) > path.stack.size()) {
                return Step::DoorChanges;
            }
            path.stack.resize(path.framePointer);
            return Step::Next;
        case OPCODE_D_TO_A:
            if (!pop(path, &value)) {
                return Step::DoorChanges;
            }
            path.returnStack.push_back(value);
            return Step::Next;
        case OPCODE_A_TO_D:
            if (path.returnStack.empty()) {
                return Step::DoorChanges;
            }
            path.stack.push_back(path.returnStack.back());
            path.returnStack.pop_back();
            return Step::Next;
        case OPCODE_SWAPA:
            if (path.returnStack.size() < 2) {
                return Step::DoorChanges;
            }
            std::swap(path.returnStack[path.returnStack.size() - 1], path.returnStack[path.returnStack.size() - 2]);
            return Step::Next;
        case OPCODE_SWAP:
            if (path.stack.size() < 2) {
                return Step::DoorChanges;
            }
            std::swap(path.stack[path.stack.size() - 1], path.stack[path.stack.size() - 2]);
            return Step::Next;
        case OPCODE_POP:
            return pop(path, &value) ? Step::Next : Step::DoorChanges;
        case OPCODE_DUP:
            if (path.stack.empty()) {
                return Step::DoorChanges;
            }
            path.stack.push_back(Value(path.stack.back()));
            return Step::Next;
        case OPCODE_POP_ADDRESS:
            if (path.returnStack.empty()) {
                return Step::DoorChanges;
            }
            path.returnStack.pop_back();
            return Step::Next;
        case OPCODE_POP_RETURN:
            if (path.returnStack.empty()) {
                return Step::DoorChanges;
            }
            value = path.returnStack.back();
            path.returnStack.pop_back();
            if (value.kind == ValueKind::End) {
                return Step::Done;
            }
            if (value.kind != ValueKind::Number) {
                return Step::DoorChanges;
            }
            path.position = value.number;
            return Step::Next;
        case OPCODE_JUMP:
            return popNumber(path, &path.position) ? Step::Next : Step::DoorChanges;
        case OPCODE_CALL: {
            int flags;
            if (!popNumber(path, &number)
                || number < 0
                || number >= script.procedureCount()
                || !script.procedureField(number, kProcedureFlagsOffset, &flags)
                || (flags & PROCEDURE_FLAG_IMPORTED) != 0
                || !script.procedureField(number, kProcedureBodyOffset, &path.position)) {
                return Step::DoorChanges;
            }
            return Step::Next;
        }
        case OPCODE_IF:
        case OPCODE_WHILE:
            return branch(path, opcode);
        case OPCODE_FETCH:
            if (!popNumber(path, &number) || !hasVariable(path, number)) {
                return Step::DoorChanges;
            }
            path.stack.push_back(Value(path.stack[path.framePointer + number]));
            return Step::Next;
        case OPCODE_STORE:
            if (!popNumber(path, &number) || !pop(path, &value) || !hasVariable(path, number)) {
                return Step::DoorChanges;
            }
            path.stack[path.framePointer + number] = value;
            return Step::Next;
        case OPCODE_FETCH_GLOBAL: {
            if (!popNumber(path, &number)) {
                return Step::DoorChanges;
            }
            auto variable = path.variables.find(number);
            path.stack.push_back(variable != path.variables.end() ? variable->second : kUnknown);
            return Step::Next;
        }
        case OPCODE_STORE_GLOBAL:
            if (!popNumber(path, &number) || !pop(path, &value)) {
                return Step::DoorChanges;
            }
            path.variables[number] = value;
            return Step::Next;
        case OPCODE_EQUAL:
        case OPCODE_NOT_EQUAL:
        case OPCODE_LESS_THAN:
        case OPCODE_LESS_THAN_EQUAL:
        case OPCODE_GREATER_THAN:
        case OPCODE_GREATER_THAN_EQUAL:
        case OPCODE_ADD:
        case OPCODE_SUB:
        case OPCODE_MUL:
        case OPCODE_DIV:
        case OPCODE_MOD:
        case OPCODE_BITWISE_AND:
        case OPCODE_BITWISE_OR:
        case OPCODE_BITWISE_XOR:
        case OPCODE_AND:
        case OPCODE_OR:
            return binary(path, opcode);
        case OPCODE_NOT:
            if (!pop(path, &value)) {
                return Step::DoorChanges;
            }
            switch (truthOf(value)) {
            case Truth::Unknown:
                path.stack.push_back(kUnknown);
                break;
            case Truth::True:
                path.stack.push_back(makeNumber(0));
                break;
            case Truth::False:
                path.stack.push_back(makeNumber(1));
                break;
            }
            return Step::Next;
        case OPCODE_NEGATE:
            if (!pop(path, &value)) {
                return Step::DoorChanges;
            }
            path.stack.push_back(value.kind == ValueKind::Number ? makeNumber(-value.number) : kUnknown);
            return Step::Next;
        case kOpDudeObj:
        case kOpSourceObj:
            path.stack.push_back({ ValueKind::Dude, 0 });
            return Step::Next;
        case kOpSelfObj:
            path.stack.push_back({ ValueKind::Self, 0 });
            return Step::Next;
        case kOpTargetObj:
            // No weapon: `scriptExecProc` gives the owner of the script.
            path.stack.push_back({ damageType == kDoorReactionUnarmed ? ValueKind::Self : ValueKind::Weapon, 0 });
            return Step::Next;
        case kOpMessageStr:
            if (!pop(path, &value) || !pop(path, &value)) {
                return Step::DoorChanges;
            }
            path.stack.push_back(kString);
            return Step::Next;
        case kOpMetarule:
            return metarule(path);
        case kOpMetarule3:
            return metarule3(path);
        }

        for (const Command& command : kCommands) {
            if (command.opcode == opcode) {
                for (int index = 0; index < command.arguments; index++) {
                    if (!pop(path, &value)) {
                        return Step::DoorChanges;
                    }
                }

                if (command.kind == CommandKind::Read) {
                    path.stack.push_back(kUnknown);
                } else if (command.kind == CommandKind::Side) {
                    reaction = DoorReaction::Script;
                }
                return Step::Next;
            }
        }

        return Step::DoorChanges;
    }

} // namespace

DoorReaction doorReactionRead(const unsigned char* data, size_t size, int damageType)
{
    ScriptFile script(data, size);
    int count = script.procedureCount();
    for (int index = 0; index < count; index++) {
        int bodyOffset;
        if (script.procedureNameIs(index, "damage_p_proc") && script.procedureField(index, kProcedureBodyOffset, &bodyOffset)) {
            return Reader(script, damageType).read(bodyOffset);
        }
    }

    return DoorReaction::None;
}

void doorReactionFindDamageTypeChanges(const unsigned char* data, size_t size, std::set<int>* weapons)
{
    ScriptFile script(data, size);

    // The code goes from the first procedure's body to the end of the file.
    int position = -1;
    int count = script.procedureCount();
    for (int index = 0; index < count; index++) {
        int bodyOffset;
        if (script.procedureField(index, kProcedureBodyOffset, &bodyOffset) && bodyOffset > 0 && (position == -1 || bodyOffset < position)) {
            position = bodyOffset;
        }
    }

    // `set_proto_data(pid, offset, value)`: the two numbers before the value.
    int previous[3] = { 0, 0, 0 };
    int numbers = 0;

    int opcode;
    while (position != -1 && script.read16(static_cast<size_t>(position), &opcode)) {
        position += 2;

        if (opcode == VALUE_TYPE_INT || opcode == VALUE_TYPE_FLOAT || opcode == VALUE_TYPE_STRING) {
            int number = 0;
            if (!script.read32(static_cast<size_t>(position), &number)) {
                return;
            }
            position += 4;

            if (opcode == VALUE_TYPE_INT) {
                previous[0] = previous[1];
                previous[1] = previous[2];
                previous[2] = number;
                numbers++;
            } else {
                numbers = 0;
            }
            continue;
        }

        if (opcode == kOpSetProtoData && numbers >= 3 && previous[1] == kProtoDataWeaponDamageType) {
            weapons->insert(previous[0]);
        }
        numbers = 0;
    }
}

} // namespace fallout
