/*
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

#include <iostream>
#include <map>

#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/RecordLayout.h>
#include <clang/Parse/Parser.h>

#include "c2ffi.h"
#include "c2ffi/ast.h"

using namespace c2ffi;

static std::string value_to_string(clang::APValue *v) {
    std::string s;
    llvm::raw_string_ostream ss(s);

    if (v->isInt() && v->getInt().isSigned())
        v->getInt().print(ss, true);
    else if (v->isInt())
        v->getInt().print(ss, false);
    else if (v->isFloat())
        ss << v->getFloat().convertToDouble();

    ss.flush();
    return s;
}

Decl *C2FFIASTConsumer::proc(const clang::Decl *d, Decl *decl) {
    if (!decl) return nullptr;

    decl->set_ns(add_decl(_ns));

    if (decl->location().empty())
        decl->set_location(_ci, d);

    if (_mid) _od->write_between();
    else _mid = true;

    _od->write(*decl);
    return decl;
}

#define PROC decl = proc(d, make_decl(x))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wunused-variable"

void C2FFIASTConsumer::HandleDecl(clang::Decl *d, const clang::NamedDecl *ns) {
    Decl *decl = nullptr;
    const clang::NamedDecl *old_ns = _ns;
    _ns = ns;

    if (d->isInvalidDecl()) {
        std::cerr << "Skipping invalid Decl:" << std::endl;
        d->dump();
        return;
    }

    if_cast(x, clang::NamespaceDecl, d) {
        PROC;
        HandleNS(x);
    } else if_cast(x, clang::VarDecl, d) PROC;
        /* C/C++ */
    else if_cast(x, clang::FieldDecl, d);
    else if_cast(x, clang::IndirectFieldDecl, d);
    else if_cast(x, clang::CXXMethodDecl, d);
    else if_cast(x, clang::FunctionTemplateDecl, d);
    else if_cast(x, clang::FunctionDecl, d) PROC;
    else if_cast(x, clang::CXXRecordDecl, d) {
        PROC;
        HandleDeclContext(x, x);
    } else if_cast(x, clang::RecordDecl, d) {
        PROC;
        HandleDeclContext(x, x);
    } else if_cast(x, clang::EnumDecl, d) PROC;
    else if_cast(x, clang::TypedefDecl, d) PROC;
    else if_cast(x, clang::ClassTemplateDecl, d);

        /* ObjC */
    else if_cast(x, clang::ObjCInterfaceDecl, d) PROC;
    else if_cast(x, clang::ObjCCategoryDecl, d) PROC;
    else if_cast(x, clang::ObjCProtocolDecl, d) PROC;
    else if_cast(x, clang::ObjCImplementationDecl, d);
    else if_cast(x, clang::ObjCMethodDecl, d);

        /* Always should be last */
    else if_cast(x, clang::NamedDecl, d) PROC;
    else decl = make_decl(d);

    delete decl;
    _ns = old_ns;
}

#pragma clang diagnostic pop

void C2FFIASTConsumer::HandleNS(const clang::NamespaceDecl *ns) {
    HandleDeclContext(ns, ns);
}

void C2FFIASTConsumer::HandleDeclContext(const clang::DeclContext *dc,
                                         const clang::NamedDecl *ns) {
    clang::DeclContext::decl_iterator it;

    for (it = dc->decls_begin(); it != dc->decls_end(); ++it)
        HandleDecl(*it, ns);
}

bool C2FFIASTConsumer::HandleTopLevelDecl(clang::DeclGroupRef d) {
    clang::DeclGroupRef::iterator it;

    for (it = d.begin(); it != d.end(); ++it)
        HandleDecl(*it);

    return true;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"

void C2FFIASTConsumer::PostProcess() {
    if (!_config.template_output) return;

    std::ofstream &out = *_config.template_output;

    out << "#include \"" << _config.filename << "\"" << std::endl;

    for (auto d : _cxx_decls) {
        if_const_cast(x, clang::ClassTemplateSpecializationDecl, d) {
            if (x->getSpecializationKind()) continue;
            if_const_cast(y, clang::ClassTemplatePartialSpecializationDecl, d)continue;

            write_template(x, out);
        }
    }
}

#pragma clang diagnostic pop

bool C2FFIASTConsumer::is_cur_decl(const clang::Decl *d) const {
    return _cur_decls.count(d);
}

Decl *C2FFIASTConsumer::make_decl(const clang::Decl *d) {
    return new UnhandledDecl("", d->getDeclKindName());
}

Decl *C2FFIASTConsumer::make_decl(const clang::NamedDecl *d) {
    return new UnhandledDecl(d->getDeclName().getAsString(),
                             d->getDeclKindName());
}

Decl *C2FFIASTConsumer::make_decl(const clang::FunctionDecl *d) {
    _cur_decls.insert(d);

    clang::FunctionTemplateSpecializationInfo *spec =
            d->getTemplateSpecializationInfo();
    const clang::Type *return_type = d->getReturnType().getTypePtr();
    auto *fd = new FunctionDecl(this,
                                d->getDeclName().getAsString(),
                                Type::make_type(this, return_type),
                                d->isVariadic(),
                                d->isInlineSpecified(),
                                d->getStorageClass(),
                                (spec ? spec->TemplateArguments : nullptr));

    for (clang::FunctionDecl::param_const_iterator i = d->param_begin();
         i != d->param_end(); i++) {
        fd->add_field(this, *i);
    }

    return fd;
}

Decl *C2FFIASTConsumer::make_decl(const clang::VarDecl *d) {
    clang::ASTContext &ctx = _ci.getASTContext();
    clang::APValue *v = nullptr;
    std::string name = d->getDeclName().getAsString();
    std::string value;
    std::string loc;
    bool is_string = false;

    if (name.substr(0, 8) == "__c2ffi_") {
        name = name.substr(8, std::string::npos);

        clang::Preprocessor &pp = _ci.getPreprocessor();
        clang::IdentifierInfo &ii = pp.getIdentifierTable().get(llvm::StringRef(name));
        const clang::MacroInfo *mi = pp.getMacroInfo(&ii);

        if (mi)
            loc = mi->getDefinitionLoc().printToString(_ci.getSourceManager());
    }

    if (d->hasInit()) {
        if (!d->getType()->isDependentType()) {
            clang::EvaluatedStmt *stmt = d->ensureEvaluatedStmt();
            auto *e = clang::cast<clang::Expr>(stmt->Value);
            if (!e->isValueDependent() &&
                ((v = d->evaluateValue()) ||
                 (v = d->getEvaluatedValue()))) {
                if (v->isLValue()) {
                    clang::APValue::LValueBase base = v->getLValueBase();
                    if (!base.isNull() && base.is<const clang::Expr *>()) {
                        const auto *expr = base.get<const clang::Expr *>();

                        if_const_cast(s, clang::StringLiteral, expr) {
                            is_string = true;

                            if (s->isAscii() || s->isUTF8()) {
                                value = s->getString();
                            } else {

                            }
                        }
                    }
                } else {
                    value = value_to_string(v);
                }
            }
        }
    }

    Type *t = Type::make_type(this, d->getTypeSourceInfo()->getType().getTypePtr());
    auto *cv = new VarDecl(name, t, value, d->hasExternalStorage(), is_string);

    if (!loc.empty())
        cv->set_location(loc);

    return cv;
}

Decl *C2FFIASTConsumer::make_decl(const clang::RecordDecl *d, bool is_toplevel) {
    std::string name = d->getDeclName().getAsString();

    if (is_toplevel && name.empty()) return nullptr;

    _cur_decls.insert(d);
    auto *rd = new RecordDecl(name, d->isUnion());
    rd->fill_record_decl(this, d);

    return rd;
}

bool is_underlying_valid(const clang::Type *t) {
    if_const_cast(e, clang::ElaboratedType, t) {
        return is_underlying_valid(e->getNamedType().getTypePtr());
    }

    if_const_cast(rt, clang::RecordType, t) {
        if (rt->getDecl()->isInvalidDecl())
            return false;
    }

    return true;
}

Decl *C2FFIASTConsumer::make_decl(const clang::TypedefDecl *d) {
    const clang::Type *t = d->getUnderlyingType().getTypePtr();

    if (is_underlying_valid(t)) {
        return new TypedefDecl(d->getDeclName().getAsString(), Type::make_type(this, t));
    } else {
        std::cerr << "Skipping typedef to invalid type:" << std::endl;
        d->dump();
        return nullptr;
    }
}

Decl *C2FFIASTConsumer::make_decl(const clang::EnumDecl *d) {
    std::string name = d->getDeclName().getAsString();

    _cur_decls.insert(d);
    auto *decl = new EnumDecl(name);

    if (name.empty()) {
        decl->set_id(add_decl(d));
    }

    for (clang::EnumDecl::enumerator_iterator i = d->enumerator_begin();
         i != d->enumerator_end(); ++i) {
        const clang::EnumConstantDecl *ecd = (*i);
        decl->add_field(ecd->getDeclName().getAsString(),
                        ecd->getInitVal().getLimitedValue());
    }

    return decl;
}

Decl *C2FFIASTConsumer::make_decl(const clang::CXXRecordDecl *d, bool is_toplevel) {
    if (!d->hasDefinition() || d->getDefinition()->isInvalidDecl())
        return nullptr;

    std::string name = d->getDeclName().getAsString();
    const clang::TemplateArgumentList *template_args = nullptr;

    if (is_toplevel && name.empty()) return nullptr;

    if_const_cast(cts, clang::ClassTemplateSpecializationDecl, d) {
        template_args = &(cts->getTemplateArgs());
    }

    bool dependent = d->isDependentType();

    _cur_decls.insert(d);
    auto *rd = new CXXRecordDecl(this, name, d->isUnion(), d->isClass(),
                                 template_args);
    rd->set_id(add_cxx_decl(d));
    rd->add_functions(this, d);

    if (!dependent) {
        rd->fill_record_decl(this, d);

        const clang::ASTRecordLayout &layout = _ci.getASTContext().getASTRecordLayout(d);

        for (clang::CXXRecordDecl::base_class_const_iterator i = d->bases_begin();
             i != d->bases_end(); ++i) {
            bool is_virtual = (*i).isVirtual();
            const clang::CXXRecordDecl *decl =
                    (*i).getType().getTypePtr()->getAsCXXRecordDecl();
            int64_t offset = 0;

            if (is_virtual)
                offset = layout.getVBaseClassOffset(decl).getQuantity();
            else
                offset = layout.getBaseClassOffset(decl).getQuantity();

            rd->add_parent(decl->getNameAsString(),
                           (CXXRecordDecl::Access) (*i).getAccessSpecifier(),
                           offset, is_virtual);
        }
    }
    return rd;
}

Decl *C2FFIASTConsumer::make_decl(const clang::NamespaceDecl *d) {
    auto *ns = new CXXNamespaceDecl(d->getNameAsString());
    ns->set_id(add_cxx_decl(d));
    ns->set_ns(add_cxx_decl(_ns));

    return ns;
}

Decl *C2FFIASTConsumer::make_decl(const clang::ObjCInterfaceDecl *d) {
    const clang::ObjCInterfaceDecl *super = d->getSuperClass();

    _cur_decls.insert(d);
    auto *r = new ObjCInterfaceDecl(d->getDeclName().getAsString(),
                                    super ? super->getDeclName().getAsString() : "",
                                    !d->hasDefinition());

    for (clang::ObjCInterfaceDecl::protocol_iterator i = d->protocol_begin();
         i != d->protocol_end(); i++)
        r->add_protocol((*i)->getDeclName().getAsString());

    for (clang::ObjCInterfaceDecl::ivar_iterator i = d->ivar_begin();
         i != d->ivar_end(); i++) {
        r->add_field(this, *i);
    }

    r->add_functions(this, d);
    return r;
}

Decl *C2FFIASTConsumer::make_decl(const clang::ObjCCategoryDecl *d) {
    auto *r = new ObjCCategoryDecl(d->getClassInterface()->getDeclName().getAsString(),
                                   d->getDeclName().getAsString());
    _cur_decls.insert(d);
    r->add_functions(this, d);
    return r;
}

Decl *C2FFIASTConsumer::make_decl(const clang::ObjCProtocolDecl *d) {
    auto *r = new ObjCProtocolDecl(d->getDeclName().getAsString());
    _cur_decls.insert(d);
    r->add_functions(this, d);
    return r;
}

unsigned int C2FFIASTConsumer::decl_id(const clang::Decl *d) const {
    auto it = _decl_map.find(d);

    if (it != _decl_map.end())
        return it->second;
    else
        return 0;
}
