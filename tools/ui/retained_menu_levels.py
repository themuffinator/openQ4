"""Shared fourth-level menu construction and reversible depth choreography."""

def top_at(ui, level):
    return ui.transform((323+(level-2)*ui.SUBPAGE_STEP)*ui.U,-63*ui.U)

def fit_page(ui, root):
    """Keep the two frame edges on the viewport and center the isotropic body."""
    for node in root['children']:
        if node['id']=='band-bottom':
            for path in node.get('paths',[]):
                for command in path['commands']:
                    for point in command.get('points',[]):
                        if isinstance(point[1],(int,float)):
                            point[1]={'fraction':1,'dp':round(point[1]-720,4)}
        elif node['id']=='prompts':
            # The bar keeps its place above the bottom band's edge, whichever
            # bar it is: the title's 22 dp legends or a page's 45 dp target.
            top=node['properties'].pop('top')['value']
            node['properties']['bottom']=ui.length(round(720-top-node['properties']['height']['value'],4))
        elif node['id']=='content':
            node['properties']['top']=ui.length(50,'%')
            node['properties']['margin-top']=ui.length(-360)
    return root

def breadcrumb(ui, doc, ident, key, slot):
    """The exact resting pose used when a title becomes a breadcrumb."""
    fits=ui.crumb_fits(key)
    for suffix,scale in [('',ui.CRUMB_SCALE)]+[('-'+lang,fit) for lang,fit in fits.items()]:
        doc.timelines.add(ident+'Rest'+suffix,1,[ui.track(ident,'transform',[(0,ui.crumb_transform(scale)),(1,ui.crumb_transform(scale))])])
    doc.events.setdefault('onActivate',[]).extend(ui.by_language(doc,ident+'Rest',fits))
    return ui.group(ident+'-clip',{**ui.absolute(left=ui.U*slot,top=0,width=ui.U*ui.CRUMB_SPAN,height=ui.U*40),'overflow':ui.keyword('hidden')},[
        ui.label(ident,key,{**ui.absolute(left=0,top=ui.U*19,width=ui.U*ui.PAGE_TITLE_W,height=ui.U*ui.PAGE_TITLE_H),
            **ui.typeface('marine',18,ui.U*ui.PAGE_TITLE_H,[1,1,1,.4]),'white-space':ui.keyword('nowrap'),'transform':ui.crumb_transform()})])

def child_transition(ui, doc, level, choices, parent_key):
    """A page may descend one notch, carrying the selected child's label.

    Use for every route from a sub-page to a detail page. Modal controls and
    sibling tabs keep their level. Back can retarget any in-flight descent.
    Each choice is (name, key) or (name, key, row): the carry starts on its
    navigation row, by default the choice's place in the list.
    """
    U=ui.U;slot=39+(level-1)*97
    doc.state['detail_title']={'type':'string','initial':choices[0][1]}
    carry=ui.label('detail-carry',choices[0][1],{**ui.absolute(left=U*slot,top=U*19,width=U*314,height=U*24.4),
        **ui.typeface('marine',24,U*24.4,ui.rgb(ui.ORANGE)),'white-space':ui.keyword('nowrap'),'opacity':ui.number(0),'transform':ui.transform()})
    doc.bind('detail-carry.text','detail-carry','text',{'state':'detail_title'})
    end=ui.transform(tx=-.25*U*314/2,ty=-.25*U*24.4/2,sx=.75,sy=.75)
    fits=ui.crumb_fits(parent_key)
    for suffix,scale in [('',ui.CRUMB_SCALE)]+[('-'+lang,fit) for lang,fit in fits.items()]:
        doc.timelines.add('detailCrumb'+suffix,350,[ui.track('title','transform',[(0,ui.transform()),(150,ui.crumb_transform(scale)),(350,ui.crumb_transform(scale))])])
    for index,(name,key,*row) in enumerate(choices):
        start=ui.transform(tx=U*(44-slot),ty=U*(192.8+30*(row[0] if row else index)-19))
        doc.timelines.add('detailEnter'+name,350,[
            ui.track('content','opacity',[(0,ui.number(1)),(150,ui.number(0)),(350,ui.number(0))]),
            ui.track('content','transform',[(0,ui.transform()),(300,ui.transform(tx=U*640)),(350,ui.transform(tx=U*640))]),
            ui.track('band-top','transform',[(0,top_at(ui,level)),(50,top_at(ui,level),ui.ACCEL),(350,top_at(ui,level+1))]),
            ui.track('detail-title-clip','width',[(0,ui.length(U*ui.PAGE_TITLE_W)),(150,ui.length(U*ui.CRUMB_SPAN)),(350,ui.length(U*ui.CRUMB_SPAN))]),
            ui.track('title','color',[(0,ui.colour([1,1,1,.5])),(150,ui.colour([1,1,1,.4])),(350,ui.colour([1,1,1,.4]))]),
            ui.track('detail-carry','opacity',[(0,ui.number(1)),(350,ui.number(1))]),
            ui.track('detail-carry','transform',[(0,start),(50,start,ui.ACCEL),(350,end)]),
            ui.track('detail-carry','color',[(0,ui.colour(ui.rgb(ui.ORANGE))),(50,ui.colour(ui.rgb(ui.ORANGE)),ui.ACCEL),(350,ui.colour([1,1,1,.5]))])])
        doc.events['detailEnter'+name]=[{'op':'setState','values':{'detail_title':key}},{'op':'playTimeline','timeline':'detailEnter'+name},*ui.by_language(doc,'detailCrumb',fits)]
    for duration in ui.SUBPAGE_REVERSE_MS:
        doc.timelines.add('detailReverse'+str(duration),duration,[
            ui.track('content','opacity',[(0,ui.number(0)),(duration,ui.number(1))]),
            ui.track('content','transform',[(0,ui.transform(tx=U*640)),(duration,ui.transform())]),
            ui.track('band-top','transform',[(0,top_at(ui,level+1),ui.ACCEL),(duration,top_at(ui,level))]),
            ui.track('title','transform',[(0,ui.crumb_transform()),(duration,ui.transform())]),
            ui.track('detail-title-clip','width',[(0,ui.length(U*ui.CRUMB_SPAN)),(duration,ui.length(U*ui.PAGE_TITLE_W))]),
            ui.track('title','color',[(0,ui.colour([1,1,1,.4])),(duration,ui.colour([1,1,1,.5]))]),
            ui.track('detail-carry','opacity',[(0,ui.number(1)),(duration,ui.number(0))])])
        doc.events['detailReverse'+str(duration)]=[{'op':'playTimeline','timeline':'detailReverse'+str(duration)}]
    # A cached parent always returns to its exact rest pose on deactivation.
    doc.timelines.add('detailReset',1,[
        ui.track('title','transform',[(0,ui.transform()),(1,ui.transform())]),
        ui.track('detail-title-clip','width',[(0,ui.length(U*ui.PAGE_TITLE_W)),(1,ui.length(U*ui.PAGE_TITLE_W))]),
        ui.track('title','color',[(0,ui.colour([1,1,1,.5])),(1,ui.colour([1,1,1,.5]))]),
        ui.track('detail-carry','opacity',[(0,ui.number(0)),(1,ui.number(0))])])
    doc.events.setdefault('onDeactivate',[]).append({'op':'playTimeline','timeline':'detailReset'})
    return carry

CHAPTER_ROWS=32  # the most levels a campaign has (Quake 4's 31): the session publishes them all
CHAPTER_LEFT,CHAPTER_TOP,CHAPTER_ROW_W,CHAPTER_LIST_H=21.0,112.0,394.0,270.0
CHAPTER_PICTURE=(428.0,112.0,183.0,137.0)  # the Load Game page's preview size
CHAPTER_PLATE_LABEL=200.0  # u: a plate's label may run on to the canvas edge
CHAPTER_DETAIL_W=200.0  # u: the explanation under the picture runs on to the canvas edge too
CHAPTER_STATUS_X=262.0  # u: the STATUS column, past the longest level name

def check_chapter_text(ui):
    """Every shipped language's chapter text fits its box on one line, and
    each explanation in the four lines under the picture."""
    from retained_saves import lines_needed,table_text
    U=ui.U
    one_line=(('#str_231280','marine',24,U*314),('#str_231289','marine',22.3,U*CHAPTER_PLATE_LABEL),('#str_230045','marine',22.3,U*CHAPTER_PLATE_LABEL),
              ('#str_231281','lowpixel',13,U*(CHAPTER_STATUS_X-4)),('#str_231282','lowpixel',13,U*(CHAPTER_ROW_W-CHAPTER_STATUS_X)),
              *((key,'lowpixel',15.8,U*(CHAPTER_ROW_W-CHAPTER_STATUS_X-4)) for key in ('#str_231283','#str_231284','#str_231285')))
    for key,face,size,width in one_line:
        texts=table_text(key)
        if sorted(texts)!=sorted(ui.LANGUAGES):
            raise SystemExit(f"{key} is missing from {sorted(set(ui.LANGUAGES)-set(texts))}")
        for language,text in texts.items():
            if ui.text_width(face,text,size)>width:
                raise SystemExit(f"{key} in {language} ({text!r}) needs {ui.text_width(face,text,size):.0f} dp of {width:.0f}")
    for key in CHAPTER_DETAIL_KEYS:
        for language,text in table_text(key).items():
            if lines_needed('lowpixel',text,15,U*CHAPTER_DETAIL_W)>4:
                raise SystemExit(f"{key} in {language} needs {lines_needed('lowpixel',text,15,U*CHAPTER_DETAIL_W)} lines of 4")

CHAPTER_DETAIL_KEYS=('#str_231286','#str_231551','#str_231288')  # saved, fresh at the current difficulty, locked
CHAPTER_FLOOR=420.0      # u: above 100 % text the picture's column runs down to the docked bottom band's edge
CHAPTER_PICTURE_LEAST=40.0   # u: a picture squeezed shorter than this gives way to the lines

def grow_chapter_detail(ui, doc, shot, name, detail, plates):
    """The chosen level's name and explanation follow the text size (section
    2's reflow): above 100 % the name's line grows and the explanation takes
    the lines the running language's longest needs at the running size, both
    under the picture, which gives them its room down to the docked bottom
    band, shrinking about its top leading corner with its aspect kept and
    giving way altogether where too little is left. START CHAPTER and BACK,
    whose room that is, stand side by side under the levels instead. Every
    binding gives the authored value at 100 % (check_text_bindings)."""
    U=ui.U;ui.text_source(doc);LARGER=ui.LARGER
    def prop(node,key): return node['properties'][key]['value']
    def larger(grown,authored): return {'op':'select','args':[LARGER,grown,authored]}
    counts=ui.wrapped_lines(CHAPTER_DETAIL_KEYS,'lowpixel',prop(detail,'font-size'),prop(detail,'width'))
    lines=ui.text_buckets({scale:ui.per_language(doc,{language:found[scale] for language,found in counts.items()}) for scale in (1.5,2)})
    line=prop(detail,'line-height')
    name_h=ui.text_times(prop(name,'height'));detail_h=ui.text_times({'op':'*','args':[lines,line]})
    back_part,picture,border=shot
    top,picture_h,picture_w=prop(back_part,'top'),prop(back_part,'height'),prop(back_part,'width')
    name_gap=prop(name,'top')-(top+picture_h);detail_gap=prop(detail,'top')-(prop(name,'top')+prop(name,'height'))
    for language,found in counts.items():
        if top+name_gap+prop(name,'height')*2+detail_gap+found[2]*line*2>U*CHAPTER_FLOOR:
            raise SystemExit(f"a chapter's explanation in {language} needs {found[2]} lines at 200 % text, more than its column holds")
    room=ui.text_sum(U*CHAPTER_FLOOR-top-name_gap-detail_gap,{'op':'*','args':[-1,name_h]},{'op':'*','args':[-1,detail_h]})
    height={'op':'max','args':[0,{'op':'min','args':[picture_h,room]}]}
    scale={'op':'/','args':[height,picture_h]}
    for node in shot:
        inset=picture_h-prop(node,'height')        # the picture inside the frame's 1 u border
        doc.bind(f"text.{node['id']}.height",node['id'],'height',larger({'op':'max','args':[0,ui.text_sum(-inset,height)]},prop(node,'height')))
        doc.bind(f"text.{node['id']}.width",node['id'],'width',
                 larger({'op':'max','args':[0,ui.text_sum(-inset,{'op':'*','args':[picture_w,scale]})]},prop(node,'width')))
        node['properties']['display']=ui.keyword('block')
        doc.bind(f"text.{node['id']}.display",node['id'],'display',
                 {'op':'select','args':[{'op':'&&','args':[LARGER,{'op':'<','args':[height,U*CHAPTER_PICTURE_LEAST]}]},'none','block']})
    name_top={'op':'select','args':[{'op':'<','args':[height,U*CHAPTER_PICTURE_LEAST]},top,ui.text_sum(top,height)]}
    doc.bind(f"text.{name['id']}.top",name['id'],'top',larger(ui.text_sum(name_gap,name_top),prop(name,'top')))
    doc.bind(f"text.{name['id']}.height",name['id'],'height',name_h)
    # The name runs on as far as the explanation does, ending in an ellipsis
    # past it: the levels' list names it in full.
    doc.bind(f"text.{name['id']}.width",name['id'],'width',larger(prop(detail,'width'),prop(name,'width')))
    name['properties']['text-overflow']=ui.keyword('clip')
    doc.bind(f"text.{name['id']}.text-overflow",name['id'],'text-overflow',larger('ellipsis','clip'))
    doc.bind(f"text.{detail['id']}.top",detail['id'],'top',larger(ui.text_sum(name_gap+detail_gap,name_top,name_h),prop(detail,'top')))
    doc.bind(f"text.{detail['id']}.height",detail['id'],'height',larger(detail_h,prop(detail,'height')))
    # The plates side by side under the levels, their labels just clear of the list.
    art=next(child for child in plates[0]['children'] if child['id'].endswith('-art'))
    label=next(child for child in art['children'] if child['id'].endswith('-label'))
    under=round(U*(CHAPTER_TOP+CHAPTER_LIST_H)+2-prop(art,'top')-prop(label,'top'),4)
    for index,plate in enumerate(plates):
        doc.bind(f"text.{plate['id']}.left",plate['id'],'left',larger(U*(CHAPTER_LEFT-8+index*208),prop(plate,'left')))
        doc.bind(f"text.{plate['id']}.top",plate['id'],'top',larger(under,prop(plate,'top')))

def chapter_view(ui, doc):
    """The campaign's chapters (docs/user/campaigns.md): every level of the
    campaign in campaign order, the ones not reached yet disabled, under the
    Load Game page's LEVEL and STATUS columns. A row's first Accept chooses
    its chapter, and the session publishes its levelshot, name and how it
    starts; Accept on the chosen one, or START CHAPTER, starts it. BACK runs
    the document's back action."""
    from retained_saves import grow_rows,picture_frame,save_row,section_plate
    check_chapter_text(ui)
    U=ui.U;ROW_H=20.0
    doc.state.update({'chapter.count':{'type':'number','initial':0},
        'chapter.first':{'type':'number','initial':0},'chapter.selected':{'type':'number','initial':-1},
        'chapter.pick':{'type':'number','initial':-1},'chapter.open':{'type':'boolean','initial':False},
        'chapter.shot':{'type':'string','initial':''},'chapter.name':{'type':'string','initial':''},
        'chapter.detail':{'type':'string','initial':''}})
    doc.session('select','campaignChapterSelect');doc.session('chapterStart','campaignChapterStart')
    rows=[]
    for i in range(CHAPTER_ROWS):
        rows.append(save_row(doc,i,'chapter',CHAPTER_ROW_W,[{'op':'action','action':'chapterStart'}],CHAPTER_STATUS_X))
        doc.state[f'chapter.row{i}.open']={'type':'boolean','initial':False}
        doc.bind(f'row-{i}.enabled',f'row-{i}','enabled',{'state':f'chapter.row{i}.open'})
    heading={**ui.typeface('lowpixel',13,U*14,[1,1,1,.4]),'white-space':ui.keyword('nowrap'),'overflow':ui.keyword('hidden')}
    headings=[ui.label('chapter-head-level','#str_231281',{**ui.absolute(left=U*CHAPTER_LEFT,top=U*(CHAPTER_TOP-15),width=U*(CHAPTER_STATUS_X-4),height=U*14),**heading}),
        ui.label('chapter-head-status','#str_231282',{**ui.absolute(left=U*(CHAPTER_LEFT+CHAPTER_STATUS_X-1),top=U*(CHAPTER_TOP-15),
            width=U*(CHAPTER_ROW_W-CHAPTER_STATUS_X),height=U*14),**heading}),
        ui.vector('chapter-rule',ui.absolute(left=U*CHAPTER_LEFT,top=U*(CHAPTER_TOP-.4)-.6,width=U*(CHAPTER_ROW_W+16),height=1.2),[
            ui.path('rule',[(0,.6),({'fraction':1},.6)],closed=False,stroke=ui.stroke(ui.solid(ui.rgb(ui.OLIVE,.26)),1.2))])]
    column=ui.group('chapter-column',{'position':ui.keyword('relative'),'display':ui.keyword('flex'),'flex-direction':ui.keyword('column')},rows)
    viewport=ui.group('chapter-list',{**ui.absolute(left=U*CHAPTER_LEFT,top=U*CHAPTER_TOP,width=U*CHAPTER_ROW_W,height=U*CHAPTER_LIST_H),
        'overflow':ui.keyword('auto')},[column])
    scrollbar=ui.objectives_scrollbar(doc,'chapter_scroll','chapter-list',ui.OLIVE,ui.ORANGE,U*(CHAPTER_LEFT+CHAPTER_ROW_W+8)-18,
        U*CHAPTER_TOP,U*CHAPTER_LIST_H,line_step=U*ROW_H,key='#str_231280')
    left,top,width,height=CHAPTER_PICTURE
    shot=picture_frame('chapter-shot',left,top,width,height)
    doc.bind('chapter-shot.image','chapter-shot','image',{'state':'chapter.shot'})
    name=ui.label('chapter-name','#str_230030',{**ui.absolute(left=U*left,top=U*(top+height+5),width=U*width,height=U*18),
        **ui.typeface('marine',18,U*18,[1,1,1,.9]),'white-space':ui.keyword('nowrap'),'overflow':ui.keyword('hidden')})
    detail=ui.label('chapter-detail','#str_230030',{**ui.absolute(left=U*left,top=U*(top+height+26),width=U*CHAPTER_DETAIL_W,height=U*62),
        **ui.typeface('lowpixel',15,20,ui.rgb(ui.OLIVE)),'white-space':ui.keyword('normal'),'overflow':ui.keyword('hidden')})
    doc.bind('chapter-name.text','chapter-name','text',{'state':'chapter.name'})
    doc.bind('chapter-detail.text','chapter-detail','text',{'state':'chapter.detail'})
    start=section_plate(doc,'chapter_start','#str_231289',left-8,top+height+90,action='chapterStart',label_w=CHAPTER_PLATE_LABEL)
    doc.bind('chapter_start.enabled','chapter_start','enabled',{'state':'chapter.open'})
    back=section_plate(doc,'chapter_back','#str_230045',left-8,top+height+120,action='back',label_w=CHAPTER_PLATE_LABEL)
    # Above 100 % text the levels' rows and the chosen level's name and
    # explanation grow with it (section 2's reflow); the headings and plates
    # are furniture and keep their size.
    grow_rows(doc,column)
    for node in [*headings,start,back]:
        ui.fixed_text(doc,node)
    grow_chapter_detail(ui,doc,shot,name,detail,(start,back))
    panel=ui.group('chapters',{**ui.absolute(left=0,top=0,width=ui.CANVAS_W,height=720)},
        [*headings,viewport,scrollbar,*shot,name,detail,start,back])
    return panel

def detail_page_motion(ui, doc):
    """A fourth-level page's own motion (spec 1.10): Back drops its title,
    sweeps its content 640 u toward the leading edge over 300 ms as the top
    band steps back a notch, and returns the CAMPAIGN crumb to the title slot
    over the last 150 ms; leaving the screen places it at rest."""
    U=ui.U
    doc.timelines.add('detailLeave',300,[
        ui.track('title','opacity',[(0,ui.number(0)),(300,ui.number(0))]),
        ui.track('content','opacity',[(0,ui.number(1)),(250,ui.number(0)),(300,ui.number(0))]),
        ui.track('content','transform',[(0,ui.transform()),(300,ui.transform(tx=-U*640))]),
        ui.track('band-top','transform',[(0,top_at(ui,4),ui.ACCEL),(300,top_at(ui,3))]),
        ui.track('crumb1-clip','width',[(0,ui.length(U*ui.CRUMB_SPAN)),(150,ui.length(U*ui.CRUMB_SPAN)),(151,ui.length(U*ui.PAGE_TITLE_W)),(300,ui.length(U*ui.PAGE_TITLE_W))]),
        ui.track('crumb1','color',[(0,ui.colour([1,1,1,.4])),(150,ui.colour([1,1,1,.4])),(300,ui.colour([1,1,1,.5]))])])
    fits=ui.crumb_fits('#str_230038')
    for suffix,scale in [('',ui.CRUMB_SCALE)]+[('-'+lang,fit) for lang,fit in fits.items()]:
        doc.timelines.add('detailCrumbReturn'+suffix,300,[ui.track('crumb1','transform',[(0,ui.crumb_transform(scale)),(150,ui.crumb_transform(scale)),(300,ui.transform())])])
    doc.events['detailLeave']=[{'op':'playTimeline','timeline':'detailLeave'},*ui.by_language(doc,'detailCrumbReturn',fits)]
    doc.timelines.add('rest',1,[ui.track('content','transform',[(0,ui.transform()),(1,ui.transform())]),
        ui.track('title','opacity',[(0,ui.number(1)),(1,ui.number(1))]),ui.track('band-top','transform',[(0,top_at(ui,4)),(1,top_at(ui,4))]),
        ui.track('crumb1','color',[(0,ui.colour([1,1,1,.4])),(1,ui.colour([1,1,1,.4]))]),
        ui.track('crumb1-clip','width',[(0,ui.length(U*ui.CRUMB_SPAN)),(1,ui.length(U*ui.CRUMB_SPAN))])])
    doc.events['onDeactivate']=[{'op':'playTimeline','timeline':'rest'}]

def detail_page_frame(ui, doc, title_key, content_parts):
    """A fourth-level page's frame: the bands a notch past the sub-page's,
    the SINGLE PLAYER and CAMPAIGN crumbs, the title at the fourth level's
    slot (233,19 u) and the content arriving over 150 ms. Returns the bands,
    the content and the titles."""
    U=ui.U
    bands=ui.framing_bands('band');bands[0]['properties']['transform']=top_at(ui,4);bands[1]['properties']['transform']=ui.transform(*ui.BOTTOM_TO_PAGE)
    bounds={**ui.absolute(top=0,width=ui.CANVAS_W,height=720),'left':ui.length(50,'%'),'margin-left':ui.length(-ui.CANVAS_W/2)}
    content=ui.group('content',{**bounds,'transform':ui.transform(),'opacity':ui.number(0)},content_parts)
    titles=[]
    for i,key in enumerate(('#str_42000','#str_230038')):
        titles.append(breadcrumb(ui,doc,'crumb'+str(i),key,39+97*i))
    titles.append(ui.page_title('title',title_key,233,19))
    titles[-1]['properties']['opacity']=ui.number(1)
    title_group=ui.group('titles',bounds,titles)
    doc.timelines.add('arrive',150,[ui.track('content','opacity',[(0,ui.number(0)),(150,ui.number(1))])])
    doc.events['onActivate'].append({'op':'playTimeline','timeline':'arrive'})
    return bands,content,title_group

def chapters_document(ui):
    """CHAPTERS, the Campaign sub-page's fourth-level child beside NEW GAME
    (spec 1.10: Campaign's campaign choice, then New Campaign and Chapters):
    the chosen campaign's levels under the SINGLE PLAYER and CAMPAIGN crumbs,
    CHAPTERS carried into the title slot. Each arrival focuses the chosen
    level, or the first; BACK and Back climb to Campaign onto CHAPTERS."""
    doc=ui.Document('openq4.campaign-chapters')
    panel=chapter_view(ui,doc)
    doc.session('back','campaignChaptersBack')
    doc.events['onBack']=[{'op':'action','action':'back'}]
    bands,content,title_group=detail_page_frame(ui,doc,'#str_231280',[panel])
    doc.events['onActivate'] += [{'op':'focus','control':'row-0'},
        *[{'op':'if','condition':{'op':'==','args':[{'state':'chapter.selected'},i]},'then':[{'op':'focus','control':f'row-{i}'}]}
          for i in range(1,CHAPTER_ROWS)]]
    detail_page_motion(ui,doc)
    result=doc.build(fit_page(ui,ui.group('screen',{**ui.FULL,'background-color':ui.colour([0,0,0,1])},[
        *ui.lit_field('field',.7),*bands,content,title_group,
        # Its parents' legends (section 13.4), since its own BACK is in the content.
        ui.page_prompts(doc,in_canvas=False)])))
    result['canvas']['minWidth']=960
    from retained_tall import TallLayout,VIEW_HEIGHT
    tall=TallLayout(result)
    tall_chapters(ui,tall,{'op':'-','args':[0,{'op':'-','args':[{'op':'/','args':[VIEW_HEIGHT,2]},360]}]})
    return result

def skill_document(ui):
    U=ui.U;doc=ui.Document('openq4.campaign-skill')
    doc.state.update({'skill':{'type':'number','initial':1},'turbo':{'type':'boolean','initial':False},
        'campaign_title':{'type':'string','initial':'#str_230039'},'skill_description':{'type':'string','initial':'#str_200019'}})
    skill_keys=['#str_200014','#str_200015','#str_200016','#str_200017','#str_41117']
    buttons=[]
    for i,key in enumerate(skill_keys):
        action=doc.session('skill'+str(i),'campaignSkill'+str(i))
        row=ui.navigation_plate(doc,'skill'+str(i),key,action=action)
        mark=ui.vector('selected'+str(i),{**ui.absolute(left=ui.NAV_LEAD+U*25,top=U*10,width=U*5,height=U*10),'opacity':ui.number(0)},
                       [ui.path('rail',[(0,0),(U*3,0),(U*3,U*10),(0,U*10)],fill=ui.solid(ui.rgb(ui.ORANGE)))])
        row['children'].append(mark);doc.bind('selected'+str(i)+'.opacity','selected'+str(i),'opacity',{'op':'select','args':[{'op':'==','args':[{'state':'skill'},i]},1,0]})
        buttons.append(row)
    for ident,key,command in [('turbo','#str_41119','campaignSkillTurbo'),('start','#str_200020','campaignSkillStart')]:
        doc.session(ident,command);buttons.append(ui.navigation_plate(doc,ident,key,action=ident))
    doc.session('back','campaignSkillBack');buttons.append(ui.navigation_plate(doc,'back','#str_230045',action='back'))
    doc.events['onBack']=[{'op':'action','action':'back'}]
    bands,content,title_group=detail_page_frame(ui,doc,'#str_230039',[
        ui.group('navigation',{**ui.absolute(left=0,top=U*172,width=U*357),'display':ui.keyword('flex'),'flex-direction':ui.keyword('column')},buttons),
        ui.label('description','#str_200019',{**ui.absolute(left=U*387,top=U*184,width=U*226,height=U*148),**ui.typeface('lowpixel',18,26,ui.rgb(ui.OLIVE)),'display':ui.keyword('block')}),
        ui.label('turbo-value','#str_41121',{**ui.absolute(left=U*270,top=U*325,width=U*77,height=U*25),**ui.typeface('marine',18,26,ui.rgb(ui.ORANGE)),'pointer-events':ui.keyword('none'),'display':ui.keyword('block')})])
    # Above 100 % text the chosen difficulty's description grows as a page's
    # content column does, and TURBO's value keeps its size on its plate.
    from retained_difficulty import DIFFICULTIES
    parts={node['id']:node for node in content['children']}
    ui.reflow_details(doc,parts['description'],[key for _,key in DIFFICULTIES])
    ui.fixed_text(doc,parts['turbo-value'])
    doc.bind('description.text','description','text',{'state':'skill_description'})
    doc.bind('turbo-value.text','turbo-value','text',{'op':'select','args':[{'state':'turbo'},'#str_41120','#str_41121']})
    doc.bind('title.text','title','text',{'state':'campaign_title'})
    # Each arrival focuses the chosen difficulty, as Restart Level's page does.
    doc.events['onActivate'] += [{'op':'focus','control':'skill0'},*[{'op':'if','condition':{'op':'==','args':[{'state':'skill'},i]},
        'then':[{'op':'focus','control':'skill'+str(i)}]} for i in range(1,len(skill_keys))]]
    detail_page_motion(ui,doc)
    result=doc.build(fit_page(ui,ui.group('screen',{**ui.FULL,'background-color':ui.colour([0,0,0,1])},[
        *ui.lit_field('field',.7),*bands,content,title_group,
        # Its parents' legends (section 13.4), since its own BACK is in the content.
        ui.page_prompts(doc,in_canvas=False)])))
    result['canvas']['minWidth']=960
    return tall_skill_page(ui,result)

# A tall view's difficulty (retained_tall), as the menu levels' column: the
# plates 1.6 times their size with TURBO's value on its plate, the chosen
# difficulty's description under them from the plates' label inset to the
# column's margin, centred between the docked bands. The content keeps its
# centred 720 dp box, so the parts move by the column's place less the box's
# top. The CHAPTERS page stands as the Load Game page does (tall_chapters):
# the chosen level's picture with START CHAPTER and BACK beside it, its name
# and explanation under it across the column, then LEVEL and STATUS and the
# levels in two-line rows down to the bottom band.
TALL_SKILL_GROW=1.6
TALL_SKILL_TOP,TALL_SKILL_BOTTOM=110.0,100.0   # dp: under the docked top band's title, over the docked bottom band
TALL_SKILL_PLATE=30.0                           # u: a navigation plate's pitch

def tall_skill_page(ui,result):
    from retained_difficulty import DIFFICULTIES
    from retained_tall import VIEW_HEIGHT,TallLayout
    U,grow=ui.U,TALL_SKILL_GROW
    tall=TallLayout(result)
    navigation=tall.nodes['navigation']
    plates_h=U*TALL_SKILL_PLATE*len(navigation['children'])*grow
    width=ui.CANVAS_W-ui.TALL_MARGIN*(1+grow)
    style=tall.nodes['description']['properties']
    size,line=style['font-size']['value'],style['line-height']['value']
    # The description takes the lines its longest text needs in its width at
    # the running text size (section 2).
    counts=ui.wrapped_lines([key for _,key in DIFFICULTIES],'lowpixel',size,width/grow)
    lines={scale:max(found[scale] for found in counts.values()) for scale in (1,1.5,2)}
    TEXT=ui.TEXT
    described={'op':'*','args':[{'op':'select','args':[{'op':'>','args':[TEXT,1.5]},lines[2]*line*grow,
               {'op':'select','args':[{'op':'>','args':[TEXT,1]},lines[1.5]*line*grow,lines[1]*line*grow]}]},TEXT]}
    box_top={'op':'-','args':[{'op':'/','args':[VIEW_HEIGHT,2]},360]}
    tall.fits(TALL_SKILL_TOP+plates_h+24+lines[1]*line*grow+TALL_SKILL_BOTTOM,
              {scale:TALL_SKILL_TOP+plates_h+24+lines[scale]*line*grow*scale+TALL_SKILL_BOTTOM for scale in (1.5,2)})
    centred={'op':'max','args':[0,{'op':'/','args':[{'op':'-','args':[VIEW_HEIGHT,ui.text_sum(TALL_SKILL_TOP+TALL_SKILL_BOTTOM+plates_h+24,
                                                                                                 described)]},2]}]}
    shift={'op':'-','args':[centred,box_top]}
    origin=(0,tall.dp('navigation','top'))
    tall.block(('navigation','turbo-value'),origin,(0,TALL_SKILL_TOP),grow,shift)
    tall.block(('description',),(tall.dp('description','left'),tall.dp('description','top')),
               (ui.TALL_MARGIN*grow,TALL_SKILL_TOP+plates_h+24),grow,shift)
    tall.set('description','width',width)
    tall.set('description','height',described)
    return result

TALL_CHAPTER_DETAIL_LINES=3   # the explanation's lines across the column (four fit its 200 u in landscape)
TALL_CHAPTER_ROWS=3           # the levels the list shows at least
TALL_CHAPTER_ROWS_LEAST={1:3,1.5:2,2:1}   # by text size, at the least portrait view that holds it

def tall_chapters(ui,tall,shift):
    from retained_saves import ROW_H,SAVE_LINES,TALL_GROW,TALL_PLATE_PITCH,TALL_PLATES,tall_list
    from retained_tall import CONTENT_W,MARGIN,VIEW_HEIGHT
    U=ui.U;left,top,width,height=CHAPTER_PICTURE;TEXT=ui.TEXT
    picture_top=TALL_SKILL_TOP
    pictures=('chapter-shot-back','chapter-shot','chapter-shot-border')
    tall.block(pictures,(U*left,U*top),(MARGIN,picture_top),TALL_GROW,shift)
    # The picture keeps the tall view's size, beside the plates; the name and
    # explanation under it grow with the text across the column.
    for ident in pictures:
        for key in ('width','height'):
            tall.set(ident,key,tall.dp(ident,key)*TALL_GROW)
        found=next(binding for binding in tall.bindings if binding['node']==ident and binding['property']=='display')
        found['value']={'op':'select','args':[tall.condition,'block',found['value']]}
    beside=MARGIN+U*width*TALL_GROW+24
    for index,ident in enumerate(('chapter_start','chapter_back')):
        tall.block((ident,),(tall.dp(ident,'left'),tall.dp(ident,'top')),(beside,picture_top+40+index*TALL_PLATE_PITCH),TALL_PLATES,shift)
    name_top=picture_top+U*height*TALL_GROW+12
    tall.block(('chapter-name',),(U*left,U*(top+height+5)),(MARGIN,name_top),TALL_GROW,shift)
    name_h=U*18*TALL_GROW
    named=ui.text_sum(shift,{'op':'*','args':[name_h,{'op':'-','args':[TEXT,1]}]})   # the name's growth past 100 %
    detail_top=name_top+name_h+4
    tall.block(('chapter-detail',),(U*left,U*(top+height+26)),(MARGIN,detail_top),TALL_GROW,named)
    line=tall.dp('chapter-detail','line-height')*TALL_GROW
    counts=ui.wrapped_lines(CHAPTER_DETAIL_KEYS,'lowpixel',tall.dp('chapter-detail','font-size'),CONTENT_W/TALL_GROW)
    lines={scale:max(TALL_CHAPTER_DETAIL_LINES if scale==1 else 0,*(found[scale] for found in counts.values())) for scale in (1,1.5,2)}
    detail_h={'op':'*','args':[{'op':'select','args':[{'op':'>','args':[TEXT,1.5]},lines[2]*line,
              {'op':'select','args':[{'op':'>','args':[TEXT,1]},lines[1.5]*line,lines[1]*line]}]},TEXT]}
    for ident in ('chapter-name','chapter-detail'):
        tall.set(ident,'width',CONTENT_W)
    tall.set('chapter-detail','height',detail_h)
    grown=ui.text_sum(named,detail_h,-lines[1]*line)   # the shift and both lines' growth past 100 %
    list_w=CONTENT_W-40
    heads={'name':(tall.dp('chapter-head-level','left'),tall.dp('chapter-head-level','top')),
           'location':(tall.dp('chapter-head-status','left'),tall.dp('chapter-head-status','top'))}
    heads_top=detail_top+lines[1]*line+24
    # The list runs to the prompt bar over the bottom band, its top in view dp.
    rows=len(SAVE_LINES);list_top=heads_top+rows*U*14*TALL_GROW+10
    below=ui.text_sum(-list_top-TALL_SKILL_BOTTOM,VIEW_HEIGHT,{'op':'*','args':[-1,ui.text_sum(grown,{'op':'*','args':[-1,shift]})]})
    tall_list(tall,'chapter',SAVE_LINES,list_w,heads,heads_top,grown,below,{'name':'chapter-head-level','location':'chapter-head-status'})
    # The list shows three levels at least, two at 150 % text and one at 200 %.
    least={scale:list_top+name_h*(scale-1)+(lines[scale]*scale-lines[1])*line+
           TALL_CHAPTER_ROWS_LEAST[scale]*rows*U*ROW_H*TALL_GROW*scale+TALL_SKILL_BOTTOM for scale in (1,1.5,2)}
    tall.fits(least[1],{scale:least[scale] for scale in (1.5,2)})
