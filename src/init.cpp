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
#include <memory>

#include <llvm/Support/Host.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/FrontendTool/Utils.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/Parse/Parser.h>
#include <clang/Parse/ParseAST.h>

#include <sys/stat.h>

#include "c2ffi/init.h"
#include "c2ffi/opt.h"

using namespace c2ffi;

void c2ffi::add_include(clang::CompilerInstance &ci, const char *path, bool is_angled,
                        bool show_error) {
    struct stat buf{};
    if(stat(path, &buf) < 0 || !S_ISDIR(buf.st_mode)) {
        if(show_error) {
            std::cerr << "Error: Not a directory: ";
            if(is_angled)
                std::cerr << "-i ";
            else
                std::cerr << "-I ";

            std::cerr << path << std::endl;
            exit(1);
        }

        return;
    }

    const clang::DirectoryEntry *dirent = ci.getFileManager().getDirectory(path);
    clang::DirectoryLookup lookup(dirent, clang::SrcMgr::C_System, false);

    ci.getPreprocessor().getHeaderSearchInfo()
        .AddSearchPath(lookup, is_angled);
}

void c2ffi::add_framework_include(clang::CompilerInstance &ci, const char *path,
                                  bool show_error) {
    struct stat buf;
    if(stat(path, &buf) < 0 || !S_ISDIR(buf.st_mode)) {
        if(show_error) {
            std::cerr << "Error: Not a directory: ";
            std::cerr << "-F ";
            std::cerr << path << std::endl;
            exit(1);
        }

        return;
    }

    ci.getHeaderSearchOpts()
      .AddPath(StringRef(path), clang::frontend::IncludeDirGroup::Angled, true, true);
}

void c2ffi::add_framework_includes(clang::CompilerInstance &ci,
                                   c2ffi::IncludeVector &v, bool show_error) {
    for(c2ffi::IncludeVector::iterator i = v.begin(); i != v.end(); i++)
        add_framework_include(ci, (*i).c_str(), show_error);
}

void c2ffi::add_includes(clang::CompilerInstance &ci,
                         c2ffi::IncludeVector &includeVector, bool is_angled,
                         bool show_error) {
    for(auto &&include : includeVector)
        add_include(ci, include.c_str(), is_angled, show_error);
}

void c2ffi::init_ci(config &c, clang::CompilerInstance &ci) {
    using clang::DiagnosticOptions;
    using clang::TextDiagnosticPrinter;
    using clang::TargetOptions;
    using clang::TargetInfo;

    DiagnosticOptions *dopt = new DiagnosticOptions;
    TextDiagnosticPrinter *tpd =
        new TextDiagnosticPrinter(llvm::errs(), dopt, false);
    ci.createDiagnostics(tpd);

    auto pto = std::make_shared<TargetOptions>();
    if(c.arch.empty())
        pto->Triple = llvm::sys::getDefaultTargetTriple();
    else
        pto->Triple = c.arch;

    TargetInfo *pti = TargetInfo::CreateTargetInfo(ci.getDiagnostics(), pto);

    clang::LangOptions &lo = ci.getLangOpts();
    switch(pti->getTriple().getEnvironment()) {
        case llvm::Triple::EnvironmentType::GNU:
            lo.GNUMode = 1;
            break;
        case llvm::Triple::EnvironmentType::MSVC:
            lo.MSVCCompat = 1;
            lo.MicrosoftExt = 1;
            break;
        default:
            std::cerr << "c2ffi warning: Unhandled environment: '"
                      << pti->getTriple().getEnvironmentName().str()
                      << "' for triple '" << c.arch
                      << "'" << std::endl;
    }
    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());
    clang::PreprocessorOptions preopts;
    ci.getInvocation().setLangDefaults(lo, c.kind, pti->getTriple(), preopts, c.std);

    ci.setTarget(pti);

    add_framework_includes(ci, c.framework_includes, true);

    ci.createPreprocessor(clang::TU_Complete);
    ci.getPreprocessorOutputOpts().ShowCPP = c.preprocess_only;
    ci.getPreprocessor().setPreprocessedOutput(c.preprocess_only);

    add_includes(ci, c.includes, false, true);
    add_includes(ci, c.sys_includes, true, true);
}
