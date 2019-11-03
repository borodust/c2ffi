/* -*- c++ -*-

   c2ffi
   Copyright (C) 2013  Ryan Pavlik

   This file is part of c2ffi.

   c2ffi is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.

   c2ffi is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with c2ffi.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "c2ffi.h"

#include <sstream>
#include <cstdarg>
#include <iomanip>

using namespace c2ffi;

namespace c2ffi {
    class JSONOutputDriver : public OutputDriver {
        int _level;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"

        void write_object(const char *type, bool open, bool close, ...) {
            va_list ap;
            char *ptr = nullptr;

            va_start(ap, close);

            if (open) os() << R"({ "tag": ")" << type << "\"";
            while ((ptr = va_arg(ap, char*))) {
                os() << ", \"" << ptr << "\": ";

                if ((ptr = va_arg(ap, char*)))
                    os() << ptr;
                else
                    break;
            }
            if (close) os() << " }";

            va_end(ap);
        }

#pragma clang diagnostic pop

        static std::string hex_str(unsigned char c) {
            std::ostringstream ss;
            ss.setf(std::ios::hex, std::ios::basefield);
            ss << "\\u00" << std::setw(2) << std::setfill('0') << (int) c;

            return ss.str();
        }

        static std::string escape_string(std::string s) {
            for (int i = s.find('\\'); i != std::string::npos;
                 i = s.find('\\', i + 2))
                s.replace(i, 1, "\\\\");

            for (int i = s.find('\"'); i != std::string::npos;
                 i = s.find('\"', i + 2))
                s.replace(i, 1, "\\\"");

            for (size_t i = 0; i < s.size(); i++)
                if ((unsigned char) (s[i]) > 127 ||
                    (unsigned char) (s[i]) < 32) {
                    s.replace(i, 1, hex_str(s[i]));
                    i += 5;
                }

            return s;
        }

        template<typename T>
        std::string str(T v) {
            std::stringstream ss;
            ss << v;
            return ss.str();
        }

        template<typename T>
        std::string qstr(T v) {
            std::stringstream ss;
            ss << '"' << escape_string(v) << '"';
            return ss.str();
        }

        void write_fields(const NameTypeVector &fields) {
            os() << '[';
            for (auto i = fields.begin();
                 i != fields.end(); i++) {
                if (i != fields.begin())
                    os() << ", ";

                write_object("field", true, false,
                             "name", qstr(i->first).c_str(),
                             "bit-offset", str(i->second->bit_offset()).c_str(),
                             "bit-size", str(i->second->bit_size()).c_str(),
                             "bit-alignment", str(i->second->bit_alignment()).c_str(),
                             "type", nullptr);
                write(*(i->second));
                write_object("", false, true, nullptr);
            }

            os() << ']';
        }

        void write_template(const TemplateMixin &d) {
            if (d.is_template()) {
                write_object("", false, false,
                             "template", nullptr);
                os() << "[";
                for (auto i =
                        d.args().begin();
                     i != d.args().end(); ++i) {
                    if (i != d.args().begin())
                        os() << ", ";

                    write_object("parameter", true, false,
                                 "type", nullptr);
                    write(*((*i)->type()));

                    if ((*i)->has_val())
                        write_object("", false, false,
                                     "value", qstr((*i)->val()).c_str(),
                                     nullptr);

                    write_object("", false, true, nullptr);
                }
                os() << "]";
            }
        }

        void write_functions(const FunctionVector &funcs) {
            os() << '[';
            for (auto i = funcs.begin();
                 i != funcs.end(); i++) {
                if (i != funcs.begin())
                    os() << ", ";
                write((const Writable &) *(*i));
            }
            os() << ']';
        }

        void write_function_header(const FunctionDecl &d) {
            const char *variadic = d.is_variadic() ? "true" : "false";
            const char *inline_ = d.is_inline() ? "true" : "false";

            write_object("function", true, false,
                         "name", qstr(d.name()).c_str(),
                         "ns", str(d.ns()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "variadic", variadic,
                         "inline", inline_,
                         "storage-class", qstr(d.storage_class()).c_str(),
                         nullptr);
            write_template(d);
        }

        void write_function_params(const FunctionDecl &d) {
            write_object("", false, false, "parameters", nullptr);
            os() << "[";
            const NameTypeVector &params = d.fields();
            for (auto i = params.begin();
                 i != params.end(); i++) {
                if (i != params.begin())
                    os() << ", ";

                write_object("parameter", true, false,
                             "name", qstr((*i).first).c_str(),
                             "type", nullptr);
                write(*(*i).second);
                write_object("", false, true, nullptr);
            }

            os() << "]";
        }

        void write_function_return(const FunctionDecl &d) {
            write_object("", false, false,
                         "return-type", nullptr);
            write(d.return_type());
            write_object("", false, true, nullptr);
        }


    public:
        explicit JSONOutputDriver(std::ostream *os)
                : OutputDriver(os), _level(0) {}

        using OutputDriver::write;

        void write_header() override {
            os() << "[" << std::endl;
        }

        void write_between() override {
            os() << "," << std::endl;
        }

        void write_footer() override {
            os() << "\n]" << std::endl;
        }

        void write_comment(const char *str) override {
            write_object("comment", true, true,
                         "text", qstr(str).c_str(),
                         nullptr);
        }

        void write_namespace(const std::string &ns) override {
            write_object("namespace", true, true,
                         "name", qstr(ns).c_str(),
                         nullptr);
            write_between();
        }

        // Types -----------------------------------------------------------
        void write(const SimpleType &t) override {
            write_object(t.name().c_str(), true, true, nullptr);
        }

        void write(const BasicType &t) override {
            write_object(t.name().c_str(), true, true,
                         "bit-size", str(t.bit_size()).c_str(),
                         "bit-alignment", str(t.bit_alignment()).c_str(),
                         nullptr);
        }

        void write(const BitfieldType &t) override {
            write_object(":bitfield", true, false,
                         "width", str(t.width()).c_str(),
                         "type", nullptr);
            write(*t.base());
            write_object("", false, true, nullptr);
        }

        void write(const PointerType &t) override {
            write_object(":pointer", true, false,
                         "type", nullptr);
            write(t.pointee());
            write_object("", false, true, nullptr);
        }

        void write(const ReferenceType &t) override {
            write_object(":reference", true, false,
                         "type", nullptr);
            write(t.pointee());
            write_object("", false, true, nullptr);
        }

        void write(const ArrayType &t) override {
            write_object(":array", true, false,
                         "type", nullptr);
            write(t.pointee());
            write_object("", false, true,
                         "size", str(t.size()).c_str(),
                         nullptr);
        }

        void write(const RecordType &t) override {
            const char *type = nullptr;

            if (t.is_union())
                type = ":union";
            else if (t.is_class())
                type = ":class";
            else
                type = ":struct";

            write_object(type, true, true,
                         "name", qstr(t.name()).c_str(),
                         "id", str(t.id()).c_str(),
                         nullptr);
        }

        void write(const EnumType &t) override {
            write_object(":enum", true, true,
                         "name", qstr(t.name()).c_str(),
                         "id", str(t.id()).c_str(),
                         nullptr);
        }

        // Decls -----------------------------------------------------------
        void write(const UnhandledDecl &d) override {
            write_object("unhandled", true, true,
                         "name", qstr(d.name()).c_str(),
                         "kind", qstr(d.kind()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         nullptr);
        }

        void write(const VarDecl &d) override {
            const char *type = d.is_extern() ? "extern" : "const";

            write_object(type, true, false,
                         "name", qstr(d.name()).c_str(),
                         "ns", str(d.ns()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "type", nullptr);

            write(d.type());

            if (!d.value().empty()) {
                write_object("", false, false,
                             "value", (d.is_string()
                                       || d.value() == "inf"
                                       || d.value() == "INF"
                                       || d.value() == "Inf"
                                       || d.value() == "nan"
                                       || d.value() == "NaN" ? qstr(d.value()) : str(d.value())).c_str(),
                             nullptr);
            }

            write_object("", false, true, nullptr);
        }

        void write(const FunctionDecl &d) override {
            write_function_header(d);

            if (d.is_objc_method())
                write_object("", false, false,
                             "scope", d.is_class_method() ? "\"class\"" : "\"instance\"",
                             nullptr);

            write_function_params(d);
            write_function_return(d);
        }

        void write(const CXXFunctionDecl &d) override {
            write_function_header(d);

            write_object("", false, false,
                         "scope", d.is_static() ? "\"class\"" : "\"instance\"",
                         "virtual", d.is_virtual() ? "true" : "false",
                         "pure", d.is_pure() ? "true" : "false",
                         "const", d.is_const() ? "true" : "false",
                         nullptr);

            write_function_params(d);
            write_function_return(d);
        }

        void write(const TypedefDecl &d) override {
            write_object("typedef", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "type", nullptr);

            write(d.type());
            write_object("", false, true, nullptr);
        }

        void write(const RecordDecl &d) override {
            const char *type = d.is_union() ? "union" : "struct";

            write_object(type, true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "id", str(d.id()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "bit-size", str(d.bit_size()).c_str(),
                         "bit-alignment", str(d.bit_alignment()).c_str(),
                         "fields", nullptr);

            write_fields(d.fields());
            write_object("", false, true, nullptr);
        }

        void write(const CXXRecordDecl &d) override {
            const char *type = d.is_union() ? "union" :
                               (d.is_class() ? "class" : "struct");

            write_object(type, true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "id", str(d.id()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "bit-size", str(d.bit_size()).c_str(),
                         "bit-alignment", str(d.bit_alignment()).c_str(),
                         nullptr);

            write_template(d);

            write_object("", false, false,
                         "parents", nullptr);

            os() << "[";

            const CXXRecordDecl::ParentRecordVector &parents = d.parents();
            for (auto i
                    = parents.begin();
                 i != parents.end(); ++i) {
                if (i != parents.begin())
                    os() << ", ";

                write_object("class", true, false,
                             "name", qstr((*i).name).c_str(),
                             "offset", str((*i).parent_offset).c_str(),
                             "is_virtual", ((*i).is_virtual ? "true" : "false"),
                             "access", nullptr);

                switch ((*i).access) {
                    case CXXRecordDecl::access_private:
                        os() << "\"private\"";
                        break;
                    case CXXRecordDecl::access_protected:
                        os() << "\"protected\"";
                        break;
                    case CXXRecordDecl::access_public:
                        os() << "\"public\"";
                        break;
                    default:
                        os() << "\"unknown\"";
                }

                write_object("", false, true, nullptr);
            }

            os() << "]";

            write_object("", false, false,
                         "fields", nullptr);

            write_fields(d.fields());
            write_object("", false, false,
                         "methods", nullptr);
            write_functions(d.functions());
            write_object("", false, true, nullptr);
        }

        void write(const CXXNamespaceDecl &d) override {
            write_object("namespace", true, true,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "id", str(d.id()).c_str(),
                         nullptr);
        }

        void write(const TypeAliasDecl &d) override {
            write_object("type-alias", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "type",
                         nullptr);
            write(d.type());
            write_object("", false, true, nullptr);
        }

        void write(const TypeAliasTemplateDecl &d) override {
            write_object("type-alias-template", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "type",
                         nullptr);

            write(d.type());
            write_template(d);
            write_object("", false, true, nullptr);
        }

        void write(const VarTemplateDecl &d) override {
            write_object("var-template", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "type",
                         nullptr);
            write(d.type());
            write_template(d);
            write_object("", false, true, nullptr);
        }

        void write(const UsingDecl &d) override {
            write_object("using", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         nullptr);
            write_object("", false, true, nullptr);
        }

        void write(const UsingShadowDecl &d) override {
            write_object("using-shadow", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         nullptr);
            write_object("", false, true, nullptr);
        }

        void write(const UsingDirectiveDecl &d) override {
            write_object("using-directive", true, true,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         nullptr);
        }

        void write(const EnumDecl &d) override {
            write_object("enum", true, false,
                         "ns", str(d.ns()).c_str(),
                         "name", qstr(d.name()).c_str(),
                         "id", str(d.id()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "fields", nullptr);

            os() << "[";
            const NameNumVector &fields = d.fields();
            for (auto i = fields.begin();
                 i != fields.end(); ++i) {
                if (i != fields.begin())
                    os() << ", ";

                write_object("field", true, true,
                             "name", qstr(i->first).c_str(),
                             "value", str(i->second).c_str(),
                             nullptr);
            }

            os() << "]";
            write_object("", false, true, nullptr);
        }

        void write(const ObjCInterfaceDecl &d) override {
            write_object(d.is_forward() ? "@class" : "@interface", true, false,
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "superclass", qstr(d.super()).c_str(),
                         "protocols", nullptr);

            os() << "[";
            const NameVector &protocols = d.protocols();
            for (auto i = protocols.begin();
                 i != protocols.end(); i++) {
                if (i != protocols.begin())
                    os() << ", ";
                os() << qstr(*i).c_str();
            }
            os() << "]";

            write_object("", false, false,
                         "ivars", nullptr);
            write_fields(d.fields());

            write_object("", false, false,
                         "methods", nullptr);
            write_functions(d.functions());

            write_object("", false, true, nullptr);
        }

        void write(const ObjCCategoryDecl &d) override {
            write_object("@category", true, false,
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "category", qstr(d.category()).c_str(),
                         "methods", nullptr);
            write_functions(d.functions());
            write_object("", false, true, nullptr);
        }

        void write(const ObjCProtocolDecl &d) override {
            write_object("@protocol", true, false,
                         "name", qstr(d.name()).c_str(),
                         "location", qstr(d.location()).c_str(),
                         "methods", nullptr);
            write_functions(d.functions());
            write_object("", false, true, nullptr);
        }
    };

    OutputDriver *MakeJSONOutputDriver(std::ostream *os) {
        return new JSONOutputDriver(os);
    }
}
