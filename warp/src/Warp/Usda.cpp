#include "Warp/Usda.h"

#include <cctype>
#include <cstdlib>

namespace Warp::usda{

const Attribute* Prim::find(const std::string& wanted) const{
    for(const Attribute& attribute : attributes) if(attribute.name == wanted) return &attribute;
    return nullptr;
}

const Value* Layer::meta(const std::string& key) const{
    for(const auto& [name, value] : metadata) if(name == key) return &value;
    return nullptr;
}

namespace{

//Citac znak po znak s rekurzivnim silaskom. Greska se baca kao string s retkom, a parse() je
//hvata - duboko ugnijezdjena vrijednost bi inace morala svaku gresku vracati kroz pet razina
class Reader{
public:
    explicit Reader(const std::string& text) : text(text){}

    void layer(Layer& out){
        skip();
        if(peek() == '(') metadata(out.metadata);
        while(true){
            skip();
            if(at >= text.size()) return;
            Prim prim;
            primitive(prim);
            out.prims.push_back(std::move(prim));
        }
    }

private:
    [[noreturn]] void fail(const std::string& what){
        size_t line = 1;
        for(size_t i = 0; i < at && i < text.size(); ++i) if(text[i] == '\n') ++line;
        throw std::string("line " + std::to_string(line) + ": " + what);
    }

    //Razmaci i komentari. "#usda 1.0" na pocetku je za ovaj citac obican komentar
    void skip(){
        while(at < text.size()){
            const char c = text[at];
            if(std::isspace(static_cast<unsigned char>(c))){ ++at; continue; }
            if(c == '#'){ while(at < text.size() && text[at] != '\n') ++at; continue; }
            break;
        }
    }

    char peek(){ skip(); return at < text.size() ? text[at] : '\0'; }

    void expect(char c){
        if(peek() != c) fail(std::string("expected '") + c + "'");
        ++at;
    }

    static bool nameChar(char c){
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':' || c == '.';
    }

    //Ime ili tip: slova, brojke, _, :, . i "[]" na kraju tipa
    std::string identifier(){
        skip();
        const size_t start = at;
        while(at < text.size() && nameChar(text[at])) ++at;
        if(start == at) fail("expected a name");
        std::string result = text.substr(start, at - start);
        if(at + 1 < text.size() && text[at] == '[' && text[at + 1] == ']'){ result += "[]"; at += 2; }
        return result;
    }

    bool numberStart(){
        const char c = peek();
        if(std::isdigit(static_cast<unsigned char>(c))) return true;
        if((c == '-' || c == '+' || c == '.') && at + 1 < text.size()){
            const char next = text[at + 1];
            return std::isdigit(static_cast<unsigned char>(next)) || next == '.';
        }
        return false;
    }

    double number(){
        skip();
        char* end = nullptr;
        const double value = std::strtod(text.c_str() + at, &end);
        if(end == text.c_str() + at) fail("expected a number");
        at = size_t(end - text.c_str());
        return value;
    }

    std::string quoted(){
        expect('"');
        std::string result;
        while(at < text.size() && text[at] != '"'){
            if(text[at] == '\\' && at + 1 < text.size()){
                const char next = text[at + 1];
                result += next == 'n' ? '\n' : next == 't' ? '\t' : next;
                at += 2;
                continue;
            }
            result += text[at++];
        }
        if(at >= text.size()) fail("unterminated string");
        ++at;
        return result;
    }

    std::string delimited(char close){
        ++at;
        const size_t start = at;
        while(at < text.size() && text[at] != close) ++at;
        if(at >= text.size()) fail(std::string("not closed with '") + close + "'");
        return text.substr(start, at++ - start);
    }

    void list(Value& out, char close){
        out.kind = Value::Kind::List;
        ++at;
        while(true){
            if(peek() == close){ ++at; return; }
            Value item;
            value(item);
            out.items.push_back(std::move(item));
            if(peek() == ','){ ++at; continue; }
            if(peek() == close){ ++at; return; }
            fail(std::string("expected ',' or '") + close + "'");
        }
    }

    //'{' ... '}': timeSamples (broj: vrijednost) ili rjecnik (tip ime = vrijednost)
    void braces(Value& out){
        ++at;
        if(peek() == '}'){ ++at; out.kind = Value::Kind::Dictionary; return; }
        if(numberStart()){
            out.kind = Value::Kind::Samples;
            while(true){
                if(peek() == '}'){ ++at; return; }
                const double time = number();
                expect(':');
                Value sample;
                value(sample);
                out.samples.push_back({time, std::move(sample)});
                if(peek() == ','){ ++at; continue; }
                if(peek() == '}'){ ++at; return; }
                fail("expected ',' or '}' in timeSamples");
            }
        }
        out.kind = Value::Kind::Dictionary;
        while(peek() != '}'){
            std::string first = identifier();
            std::string key = first;
            if(peek() != '=') key = identifier();      //"string ime = ..." - prvo je tip
            expect('=');
            Value entry;
            value(entry);
            out.entries.push_back({key, std::move(entry)});
            if(peek() == ',') ++at;
        }
        ++at;
    }

    void value(Value& out){
        const char c = peek();
        if(numberStart()){ out.kind = Value::Kind::Number; out.number = number(); return; }
        if(c == '"'){ out.kind = Value::Kind::String; out.text = quoted(); return; }
        if(c == '@'){ out.kind = Value::Kind::String; out.text = delimited('@'); return; }
        if(c == '<'){ out.kind = Value::Kind::String; out.text = delimited('>'); return; }
        if(c == '('){ list(out, ')'); return; }
        if(c == '['){ list(out, ']'); return; }
        if(c == '{'){ braces(out); return; }
        //Golo ime: None, true, false, inf...
        out.kind = Value::Kind::String;
        out.text = identifier();
        if(out.text == "inf"){ out.kind = Value::Kind::Number; out.number = 1e300; }
    }

    //'(' ime = vrijednost ... ')' - metapodaci sloja, prima ili atributa. Goli string je opis
    void metadata(std::vector<std::pair<std::string, Value>>& out){
        expect('(');
        while(peek() != ')'){
            if(at >= text.size()) fail("unterminated metadata");
            if(peek() == '"'){ quoted(); continue; }
            std::string key = identifier();
            //"prepend apiSchemas = [...]" i slicno: rijec ispred imena
            if(peek() != '=') key = identifier();
            expect('=');
            Value entry;
            value(entry);
            out.push_back({key, std::move(entry)});
            if(peek() == ';' || peek() == ',') ++at;
        }
        ++at;
    }

    void primitive(Prim& out){
        out.specifier = identifier();
        if(out.specifier != "def" && out.specifier != "over" && out.specifier != "class") fail("expected 'def'");
        if(peek() != '"') out.type = identifier();
        out.name = quoted();
        if(peek() == '('){
            std::vector<std::pair<std::string, Value>> ignored;
            metadata(ignored);
        }
        expect('{');
        while(true){
            const char c = peek();
            if(c == '}'){ ++at; return; }
            if(at >= text.size()) fail("prim '" + out.name + "' is not closed");
            //Djete ili atribut: dijete pocinje s def/over/class i ima ime pod navodnicima
            const size_t mark = at;
            const std::string word = identifier();
            if((word == "def" || word == "over" || word == "class")){
                at = mark;
                Prim child;
                primitive(child);
                out.children.push_back(std::move(child));
                continue;
            }
            std::string type = word;
            while(type == "uniform" || type == "custom" || type == "varying" || type == "prepend" ||
                  type == "append" || type == "delete") type = identifier();
            if(type == "rel"){
                std::string name = identifier();
                if(peek() == '='){ ++at; Value target; value(target); }
                (void)name;
                continue;
            }
            std::string name = identifier();
            bool isSamples = false;
            const std::string suffix = ".timeSamples";
            if(name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0){
                name.resize(name.size() - suffix.size());
                isSamples = true;
            }
            Attribute* attribute = nullptr;
            for(Attribute& existing : out.attributes) if(existing.name == name){ attribute = &existing; break; }
            if(!attribute){
                out.attributes.push_back(Attribute{type, name, {}, {}});
                attribute = &out.attributes.back();
            }
            if(peek() == '='){
                ++at;
                value(isSamples ? attribute->samples : attribute->value);
            }
            if(peek() == '('){
                std::vector<std::pair<std::string, Value>> ignored;
                metadata(ignored);
            }
        }
    }

    const std::string& text;
    size_t at = 0;
};

}

bool parse(const std::string& text, Layer& out, std::string& error){
    out = Layer{};
    try{
        Reader(text).layer(out);
    }catch(const std::string& problem){
        error = problem;
        return false;
    }
    return true;
}

}
