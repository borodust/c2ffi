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

#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/DeclTemplate.h>
#include "c2ffi.h"
#include "c2ffi/ast.h"

using namespace c2ffi;

Type::Type(const clang::CompilerInstance &ci, const clang::Type *t)
    : _ci(ci), _type(t), _id(0), _bit_offset(0) { }

std::string Type::metatype() const {
    if(_type)
        return std::string("<") + _type->getTypeClassName() + ">";
    else
        return std::string("<none>");
}

RecordType::RecordType(C2FFIASTConsumer *ast,
                       const clang::Type *t,
                       std::string name, bool is_union,
                       bool is_class,
                       const clang::TemplateArgumentList *arglist)
    : SimpleType(ast->ci(), t, name),
      TemplateMixin(ast, arglist),
      _is_union(is_union),
      _is_class(is_class) { }


DeclType::DeclType(clang::CompilerInstance &ci, const clang::Type *t,
         Decl *d, const clang::Decl *cd)
    : Type(ci, t), _d(d) {
    _d->set_location(ci, cd);
}

static std::string make_builtin_name(const clang::BuiltinType *bt) {
    clang::PrintingPolicy pp = clang::PrintingPolicy(clang::LangOptions());
    std::string name = std::string(":") + bt->getNameAsCString(pp);

    for(int i = 0; i < name.size(); i++)
        if(name[i] == ' ')
            name[i] = '-';

    return name;
}

Type* Type::make_type(C2FFIASTConsumer *ast, const clang::Type *t) {
    clang::CompilerInstance &ci = ast->ci();
    clang::ASTContext &ctx = ci.getASTContext();

    /*
    std::cerr << "type: " << t->getTypeClassName() << std::endl
              << "    ";
    t->dump();

    if(t->isRecordType()) {
        t->getAsCXXRecordDecl()->dump();
    }
    */

    /*** Order is important here ***/

    if(t->isVoidType())
        return new SimpleType(ci, t, ":void");

    if_const_cast(td, clang::TypedefType, t) {
        const clang::TypedefNameDecl *tdd = td->getDecl();
        return new SimpleType(ci, td, tdd->getDeclName().getAsString());
    }

    if_const_cast(tt, clang::SubstTemplateTypeParmType, t) {
        if(tt != tt->desugar().getTypePtr())
            return make_type(ast, tt->desugar().getTypePtr());
    }
    /*
    if_const_cast(tst, clang::TemplateSpecializationType, t) {
        std::cerr << "TST:" << std::endl;

        for(int i = 0; i < tst->getNumArgs(); i++) {
            const clang::TemplateArgument& arg = tst->getArg(i);

            if(arg.getKind() == clang::TemplateArgument::Type) {
                const clang::QualType qt = arg.getAsType();
                std::cerr << "qt[" << i << "] -> " << qt.getAsString()
                          << std::endl;
            } else if(arg.getKind() == clang::TemplateArgument::Declaration) {
                const clang::ValueDecl *vd = arg.getAsDecl();
                std::cerr << "valuedecl[" << i << "] -> "
                          << vd->getType().getAsString() << std::endl;
            } else if(arg.getKind() == clang::TemplateArgument::Expression) {
                const clang::Expr *expr = arg.getAsExpr();
                std::cerr << "expr[" << i << "] -> "
                          << "caneval: "
                          << (expr->isEvaluatable(ctx) ? "yes" : "no");

                if(expr->isEvaluatable(ctx)) {
                    llvm::APSInt i;
                    expr->EvaluateAsInt(i, ctx);
                    std::cerr << " => " << i.toString(10);
                } else {
                    std::cerr << std::endl << "   ";
                    expr->dump();
                }

                std::cerr << std::endl;
            } else {
                std::cerr << "arg[" << i << "] -> "
                          << arg.getKind() << std::endl;
            }
        }

        std::cerr << std::endl;
        //return new SimpleType(ci, t, std::string("<template-specialization-type>"));
    }
    */

    if(t->isBuiltinType()) {
        const clang::BuiltinType *bt = llvm::dyn_cast<clang::BuiltinType>(t);
        if(!bt) return new SimpleType(ci, t, std::string("<unknown-builtin-type:") +
                                      t->getTypeClassName() + ">");

        return new SimpleType(ci, t, make_builtin_name(bt));
    }

    if_const_cast(e, clang::ElaboratedType, t)
        return make_type(ast, e->getNamedType().getTypePtr());

    if(t->isFunctionPointerType())
        return new SimpleType(ci, t, ":function-pointer");

    if(t->isFunctionType())
        return new SimpleType(ci, t, ":function");

    if(t->isPointerType())
        return new PointerType(ci, t, make_type(ast, t->getPointeeType().getTypePtr()));

    if(t->isReferenceType())
        return new ReferenceType(ci, t, make_type(ast, t->getPointeeType().getTypePtr()));

    if_const_cast(rt, clang::RecordType, t) {
        clang::RecordDecl *rd = rt->getDecl();

        ast->add_cxx_decl(rd);

        if(rd->isThisDeclarationADefinition() &&
           rd->isEmbeddedInDeclarator() &&
           !ast->is_cur_decl(rd)) {
            return new DeclType(ci, t, ast->make_decl(rd, false), rd);
        } else {
            std::string name = rd->getDeclName().getAsString();
            RecordType *rec = new RecordType(ast, t, name, rd->isUnion(), rd->isClass());

            if(name == "" || rd->isClass())
                rec->set_id(ast->decl_id(rd));

            return rec;
        }
    }

    if_const_cast(tt, clang::TemplateSpecializationType, t) {
        if(tt != tt->desugar().getTypePtr())
            return make_type(ast, tt->desugar().getTypePtr());
    }

    if_const_cast(ed, clang::EnumType, t) {
        std::string name = ed->getDecl()->getDeclName().getAsString();

        if(ed->getDecl()->isThisDeclarationADefinition() &&
           !ast->is_cur_decl(ed->getDecl()))
            return new DeclType(ci, t, ast->make_decl(ed->getDecl(), false),
                                ed->getDecl());
        else {
            EnumType *et = new EnumType(ci, t, name);

            if(name == "")
                et->set_id(ast->decl_id(ed->getDecl()));

            return et;
        }
    }

    if_const_cast(ca, clang::ConstantArrayType, t)
        return new ArrayType(ci, ca,
                             make_type(ast, ca->getElementType().getTypePtr()),
                             ca->getSize().getLimitedValue());

    if_const_cast(ca, clang::IncompleteArrayType, t)
        return new PointerType(ci, ca,
                               make_type(ast, ca->getElementType().getTypePtr()));

    if_const_cast(op, clang::ObjCObjectPointerType, t)
        return new PointerType(ci, op,
                               make_type(ast, op->getPointeeType().getTypePtr()));

    if_const_cast(ob, clang::ObjCObjectType, t)
        return new SimpleType(ci, t, ob->getInterface()->getDeclName().getAsString());

 error:
    return new SimpleType(ci, t, std::string("<unknown-type:") +
                          t->getTypeClassName() + ">");
}

bool PointerType::is_string() const {
    if_const_cast(bt, clang::BuiltinType, _pointee->_type) {
        clang::BuiltinType::Kind k = bt->getKind();

        switch(k) {
            case clang::BuiltinType::Char_U:
            case clang::BuiltinType::UChar:
            case clang::BuiltinType::Char16:
            case clang::BuiltinType::Char32:
            case clang::BuiltinType::Char_S:
            case clang::BuiltinType::SChar:
            case clang::BuiltinType::WChar_S:
                return true;
            default:
                return false;
        }
    }

    return false;
}

void DeclType::write(OutputDriver &od) const {
    if(_d)
        _d->write(od);
}

