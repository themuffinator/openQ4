#!/usr/bin/env python3
"""Extract the actual retained-view methods for the portable Runtime regression."""
import argparse
from pathlib import Path

from filesystem_case_segments import function_body


def project_view(source):
    structs = source[source.index('struct retainedUIPreparedEditData;'):source.index('\nnamespace {')]
    globals = '''EngineHost host;std::vector<retainedUIView_t*> views;
bool resourcesRefreshing=false,rootSubmissionPending=false;std::uint64_t rootSubmissionFrame=0;
int restartGeneration=-1,languageGeneration=-1;std::uint64_t languageRevision=0,loadedLanguageRevision=0;
'''
    helpers = source[source.index('const std::thread::id editThread='):source.index('\nvoid RecordProfile(')]
    signatures = [
        'bool RegisteredView(', 'bool LoadViewDocument(', 'void ReportViewDiagnostics(', 'bool RefreshResources(',
        'retainedUIView_t* RetainedUI_CreateView(', 'void RetainedUI_DestroyView(',
        'openq4::ui::Runtime* RetainedUI_ViewRuntime(', 'bool RetainedUI_LoadView(',
        'bool RetainedUI_PrepareView(', 'bool RetainedUI_QueryEditIdentity(',
        'retainedUIPreparedEdit_t* PrepareViewEdit(', 'retainedUIPreparedEdit_t* RetainedUI_PrepareEdit(',
        'retainedUIPreparedEdit_t* RetainedUI_PrepareHistory(', 'bool RetainedUI_PublishEdit(',
        'void RetainedUI_DestroyPreparedEdit(', 'void RetainedUI_Shutdown()',
    ]
    return structs + globals + helpers + '\n'.join(function_body(source, name) for name in signatures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.resolve().write_text(project_view(args.source.read_text(encoding='utf-8')),
                                    encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
