// Minimal JSON parser for APBConfigTool - drop-in subset of nlohmann/json API
// MIT-compatible, no dependencies, C++17
#pragma once
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>
#include <sstream>
#include <cstdint>
#include <cmath>
#include <optional>
#include <algorithm>
#include <cctype>

namespace nlohmann {

struct json {
    enum class value_t { null, boolean, number, string, array, object };

    using object_t = std::map<std::string, json>;
    using array_t  = std::vector<json>;
    using value_v  = std::variant<std::monostate, bool, double, std::string, array_t, object_t>;

    value_v _v;
    value_t _t = value_t::null;

    json()                           : _t(value_t::null)    {}
    json(std::nullptr_t)             : _t(value_t::null)    {}
    json(bool b)                     : _v(b),   _t(value_t::boolean) {}
    json(double d)                   : _v(d),   _t(value_t::number)  {}
    json(int i)                      : _v(double(i)), _t(value_t::number)  {}
    json(int64_t i)                  : _v(double(i)), _t(value_t::number)  {}
    json(const char* s)              : _v(std::string(s)), _t(value_t::string) {}
    json(const std::string& s)       : _v(s),   _t(value_t::string)  {}
    json(std::string&& s)            : _v(std::move(s)), _t(value_t::string) {}
    json(const array_t& a)           : _v(a),   _t(value_t::array)   {}
    json(array_t&& a)                : _v(std::move(a)), _t(value_t::array)  {}
    json(const object_t& o)          : _v(o),   _t(value_t::object)  {}
    json(object_t&& o)               : _v(std::move(o)), _t(value_t::object) {}

    value_t type() const { return _t; }
    bool is_null()    const { return _t == value_t::null;    }
    bool is_boolean() const { return _t == value_t::boolean; }
    bool is_number()  const { return _t == value_t::number;  }
    bool is_string()  const { return _t == value_t::string;  }
    bool is_array()   const { return _t == value_t::array;   }
    bool is_object()  const { return _t == value_t::object;  }

    bool        get_bool()   const { return std::get<bool>(_v); }
    double      get_double() const { return std::get<double>(_v); }
    const std::string& get_string() const { return std::get<std::string>(_v); }
    const array_t&  get_array()  const { return std::get<array_t>(_v); }
    const object_t& get_object() const { return std::get<object_t>(_v); }
    array_t&  get_array()  { return std::get<array_t>(_v); }
    object_t& get_object() { return std::get<object_t>(_v); }

    template<typename T> T get() const;

    // Object access
    bool contains(const std::string& k) const {
        if(!is_object()) return false;
        return get_object().count(k) > 0;
    }
    const json& at(const std::string& k) const {
        return get_object().at(k);
    }
    json& operator[](const std::string& k) {
        if(_t == value_t::null){ _t = value_t::object; _v = object_t{}; }
        return get_object()[k];
    }
    const json& operator[](const std::string& k) const {
        static json _null;
        auto& o = get_object();
        auto it = o.find(k);
        return it == o.end() ? _null : it->second;
    }
    // Case-insensitive find
    const json* find_ci(const std::string& k) const {
        if(!is_object()) return nullptr;
        std::string kl = k; for(auto& c:kl) c=char(std::tolower(c));
        for(auto& [key,val]:get_object()){
            std::string kk=key; for(auto& c:kk) c=char(std::tolower(c));
            if(kk==kl) return &val;
        }
        return nullptr;
    }

    // Array access
    const json& operator[](size_t i) const { return get_array()[i]; }
    json&       operator[](size_t i)       { return get_array()[i]; }
    size_t size() const {
        if(is_array())  return get_array().size();
        if(is_object()) return get_object().size();
        return 0;
    }
    bool empty() const { return size()==0; }

    // Iterators (array)
    auto begin()       { return get_array().begin(); }
    auto end()         { return get_array().end();   }
    auto begin() const { return get_array().begin(); }
    auto end()   const { return get_array().end();   }

    // value_or helpers
    double value(const std::string& k, double def) const {
        if(!is_object()) return def;
        auto it = get_object().find(k);
        if(it==get_object().end()) return def;
        if(it->second.is_number()) return it->second.get_double();
        return def;
    }
    std::string value(const std::string& k, const std::string& def) const {
        if(!is_object()) return def;
        auto it = get_object().find(k);
        if(it==get_object().end()) return def;
        if(it->second.is_string()) return it->second.get_string();
        return def;
    }
    std::string value(const std::string& k, const char* def) const {
        return value(k, std::string(def));
    }

    // ── Parser ────────────────────────────────────────────────────────────────
    static json parse(const std::string& s) {
        size_t i=0; skipWs(s,i);
        json v = parseValue(s,i);
        return v;
    }
    static json parse(const std::string& s, bool& ok) {
        try { ok=true; return parse(s); }
        catch(...){ ok=false; return json{}; }
    }

private:
    static void skipWs(const std::string& s, size_t& i){
        while(i<s.size()&&(s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i;
    }
    static json parseValue(const std::string& s, size_t& i){
        skipWs(s,i);
        if(i>=s.size()) throw std::runtime_error("EOF");
        char c=s[i];
        if(c=='"')  return parseString(s,i);
        if(c=='{')  return parseObject(s,i);
        if(c=='[')  return parseArray(s,i);
        if(c=='t')  { i+=4; return json(true);  }
        if(c=='f')  { i+=5; return json(false); }
        if(c=='n')  { i+=4; return json(nullptr); }
        return parseNumber(s,i);
    }
    static std::string parseString(const std::string& s, size_t& i){
        ++i; std::string r;
        while(i<s.size()&&s[i]!='"'){
            if(s[i]=='\\'){ ++i;
                switch(s[i]){
                case '"':  r+='"';  break; case '\\': r+='\\'; break;
                case '/':  r+='/';  break; case 'b':  r+='\b'; break;
                case 'f':  r+='\f'; break; case 'n':  r+='\n'; break;
                case 'r':  r+='\r'; break; case 't':  r+='\t'; break;
                case 'u': {
                    unsigned cp=0;
                    for(int j=0;j<4;++j){
                        ++i; char h=s[i];
                        cp<<=4;
                        if(h>='0'&&h<='9') cp|=h-'0';
                        else if(h>='a'&&h<='f') cp|=h-'a'+10;
                        else if(h>='A'&&h<='F') cp|=h-'A'+10;
                    }
                    // encode as UTF-8
                    if(cp<0x80) r+=char(cp);
                    else if(cp<0x800){ r+=char(0xC0|(cp>>6)); r+=char(0x80|(cp&0x3F)); }
                    else { r+=char(0xE0|(cp>>12)); r+=char(0x80|((cp>>6)&0x3F)); r+=char(0x80|(cp&0x3F)); }
                    break;
                }
                } ++i;
            } else { r+=s[i++]; }
        }
        ++i; return r;
    }
    static json parseObject(const std::string& s, size_t& i){
        ++i; object_t obj; skipWs(s,i);
        if(i<s.size()&&s[i]=='}'){++i;return json(obj);}
        while(true){
            skipWs(s,i);
            if(s[i]!='"') throw std::runtime_error("expected key");
            std::string key=parseString(s,i);
            skipWs(s,i); ++i; // ':'
            skipWs(s,i);
            obj[key]=parseValue(s,i);
            skipWs(s,i);
            if(s[i]=='}'){++i;break;}
            ++i; // ','
        }
        return json(obj);
    }
    static json parseArray(const std::string& s, size_t& i){
        ++i; array_t arr; skipWs(s,i);
        if(i<s.size()&&s[i]==']'){++i;return json(arr);}
        while(true){
            skipWs(s,i);
            arr.push_back(parseValue(s,i));
            skipWs(s,i);
            if(s[i]==']'){++i;break;}
            ++i; // ','
        }
        return json(arr);
    }
    static json parseNumber(const std::string& s, size_t& i){
        size_t start=i;
        if(s[i]=='-') ++i;
        while(i<s.size()&&std::isdigit((unsigned char)s[i])) ++i;
        if(i<s.size()&&s[i]=='.'){++i; while(i<s.size()&&std::isdigit((unsigned char)s[i])) ++i;}
        if(i<s.size()&&(s[i]=='e'||s[i]=='E')){
            ++i; if(i<s.size()&&(s[i]=='+'||s[i]=='-')) ++i;
            while(i<s.size()&&std::isdigit((unsigned char)s[i])) ++i;
        }
        return json(std::stod(s.substr(start,i-start)));
    }
};

template<> inline double      json::get<double>()      const { return get_double(); }
template<> inline int         json::get<int>()          const { return int(get_double()); }
template<> inline bool        json::get<bool>()         const { return get_bool(); }
template<> inline std::string json::get<std::string>()  const { return get_string(); }

} // namespace nlohmann

// Common alias
using json = nlohmann::json;
