"""Shared fourth-level menu construction and reversible depth choreography."""

def top_at(ui, level):
    return ui.transform((323+(level-2)*ui.SUBPAGE_STEP)*ui.U,-63*ui.U)

def fit_page(ui, root):
    """Keep the two frame edges on the viewport and center the isotropic body."""
    for node in root['children']:
        if node['id']=='band-bottom':
            for path in node['paths']:
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
    for index,(name,key) in enumerate(choices):
        start=ui.transform(tx=U*(44-slot),ty=U*(192.8+30*index-19))
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
    for key in ('#str_231286','#str_231287','#str_231288'):
        for language,text in table_text(key).items():
            if lines_needed('lowpixel',text,15,U*CHAPTER_DETAIL_W)>4:
                raise SystemExit(f"{key} in {language} needs {lines_needed('lowpixel',text,15,U*CHAPTER_DETAIL_W)} lines of 4")

def chapter_view(ui, doc):
    """The campaign page's chapters (docs/user/campaigns.md): every level of
    the campaign in campaign order, the ones not reached yet disabled, under
    the Load Game page's LEVEL and STATUS columns. A row's first Accept
    chooses its chapter, and the session publishes its levelshot, name and
    how it starts; Accept on the chosen one, or START CHAPTER, starts it.
    BACK and the page's Back return to the difficulty."""
    from retained_saves import picture_frame,save_row,section_plate
    check_chapter_text(ui)
    U=ui.U;ROW_H=20.0
    doc.state.update({'view':{'type':'number','initial':0},'chapter.count':{'type':'number','initial':0},
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
    back=section_plate(doc,'chapter_back','#str_230045',left-8,top+height+120,event='closeChapters',label_w=CHAPTER_PLATE_LABEL)
    panel=ui.group('chapters',{**ui.absolute(left=0,top=0,width=ui.CANVAS_W,height=720),'display':ui.keyword('none')},
        [*headings,viewport,scrollbar,*shot,name,detail,start,back])
    doc.bind('chapters.display','chapters','display',{'op':'select','args':[{'op':'==','args':[{'state':'view'},1]},'block','none']})
    # Opening the chapters focuses the chosen one; closing them, CHAPTERS.
    doc.events['openChapters']=[{'op':'setState','values':{'view':1}},{'op':'focus','control':'row-0'},
        *[{'op':'if','condition':{'op':'==','args':[{'state':'chapter.selected'},i]},'then':[{'op':'focus','control':f'row-{i}'}]}
          for i in range(1,CHAPTER_ROWS)]]
    doc.events['closeChapters']=[{'op':'setState','values':{'view':0}},{'op':'focus','control':'chapters-plate'}]
    return panel

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
    # CHAPTERS opens the campaign's levels in the difficulty's place.
    buttons.append(ui.navigation_plate(doc,'chapters-plate','#str_231280',event='openChapters'))
    doc.session('back','campaignSkillBack');buttons.append(ui.navigation_plate(doc,'back','#str_230045',action='back'))
    chapters=chapter_view(ui,doc)
    doc.events['onBack']=[{'op':'if','condition':{'op':'==','args':[{'state':'view'},1]},
        'then':[{'op':'call','event':'closeChapters'}],'else':[{'op':'action','action':'back'}]}]
    bands=ui.framing_bands('band');bands[0]['properties']['transform']=top_at(ui,4);bands[1]['properties']['transform']=ui.transform(*ui.BOTTOM_TO_PAGE)
    bounds={**ui.absolute(top=0,width=ui.CANVAS_W,height=720),'left':ui.length(50,'%'),'margin-left':ui.length(-ui.CANVAS_W/2)}
    content=ui.group('content',{**bounds,'transform':ui.transform(),'opacity':ui.number(0)},[
        ui.group('navigation',{**ui.absolute(left=0,top=U*172,width=U*357),'display':ui.keyword('flex'),'flex-direction':ui.keyword('column')},buttons),
        ui.label('description','#str_200019',{**ui.absolute(left=U*387,top=U*184,width=U*226,height=U*148),**ui.typeface('lowpixel',18,26,ui.rgb(ui.OLIVE)),'display':ui.keyword('block')}),
        ui.label('turbo-value','#str_41121',{**ui.absolute(left=U*270,top=U*325,width=U*77,height=U*25),**ui.typeface('marine',18,26,ui.rgb(ui.ORANGE)),'pointer-events':ui.keyword('none'),'display':ui.keyword('block')}),
        chapters])
    difficulty={'op':'==','args':[{'state':'view'},0]}
    doc.bind('navigation.display','navigation','display',{'op':'select','args':[difficulty,'flex','none']})
    for ident in ('description','turbo-value'):
        doc.bind(ident+'.display',ident,'display',{'op':'select','args':[difficulty,'block','none']})
    doc.bind('description.text','description','text',{'state':'skill_description'})
    doc.bind('turbo-value.text','turbo-value','text',{'op':'select','args':[{'state':'turbo'},'#str_41120','#str_41121']})
    titles=[]
    for i,key in enumerate(('#str_42000','#str_230038')):
        titles.append(breadcrumb(ui,doc,'crumb'+str(i),key,39+97*i))
    titles.append(ui.page_title('title','#str_230039',233,19))
    titles[-1]['properties']['opacity']=ui.number(1)
    doc.bind('title.text','title','text',{'state':'campaign_title'})
    title_group=ui.group('titles',bounds,titles)
    doc.timelines.add('arrive',150,[ui.track('content','opacity',[(0,ui.number(0)),(150,ui.number(1))])])
    doc.events['onActivate'].append({'op':'playTimeline','timeline':'arrive'})
    # Each arrival focuses the chosen difficulty, as Restart Level's page does.
    doc.events['onActivate'] += [{'op':'setState','values':{'view':0}},{'op':'focus','control':'skill0'},*[{'op':'if','condition':{'op':'==','args':[{'state':'skill'},i]},
        'then':[{'op':'focus','control':'skill'+str(i)}]} for i in range(1,len(skill_keys))]]
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
# top. CHAPTERS stands as the Load Game page does: the chosen level's
# picture with START CHAPTER and BACK beside it, its name and explanation
# under it across the column, then LEVEL and STATUS and the levels in
# two-line rows down to the bottom band.
TALL_SKILL_GROW=1.6
TALL_SKILL_TOP,TALL_SKILL_BOTTOM=110.0,100.0   # dp: under the docked top band's title, over the docked bottom band
TALL_SKILL_PLATE=30.0                           # u: a navigation plate's pitch

def tall_skill_page(ui,result):
    from retained_difficulty import DIFFICULTIES
    from retained_saves import lines_needed,table_text
    from retained_tall import VIEW_HEIGHT,TallLayout
    U,grow=ui.U,TALL_SKILL_GROW
    tall=TallLayout(result)
    navigation=tall.nodes['navigation']
    plates_h=U*TALL_SKILL_PLATE*len(navigation['children'])*grow
    width=ui.CANVAS_W-ui.TALL_MARGIN*(1+grow)
    style=tall.nodes['description']['properties']
    size,line=style['font-size']['value'],style['line-height']['value']
    lines=max(lines_needed('lowpixel',text,size,width/grow) for _,key in DIFFICULTIES for text in table_text(key).values())
    described=lines*line*grow
    box_top={'op':'-','args':[{'op':'/','args':[VIEW_HEIGHT,2]},360]}
    shift={'op':'-','args':[tall.centred(plates_h+24+described,TALL_SKILL_TOP,TALL_SKILL_BOTTOM),box_top]}
    origin=(0,tall.dp('navigation','top'))
    tall.block(('navigation','turbo-value'),origin,(0,TALL_SKILL_TOP),grow,shift)
    tall.block(('description',),(tall.dp('description','left'),tall.dp('description','top')),
               (ui.TALL_MARGIN*grow,TALL_SKILL_TOP+plates_h+24),grow,shift)
    tall.set('description','width',width)
    tall.set('description','height',described)
    tall_chapters(ui,tall,{'op':'-','args':[0,box_top]})
    return result

TALL_CHAPTER_DETAIL_LINES=3   # the explanation's lines across the column (four fit its 200 u in landscape)

def tall_chapters(ui,tall,shift):
    from retained_saves import SAVE_LINES,TALL_GROW,TALL_PLATE_PITCH,TALL_PLATES,tall_list
    from retained_tall import CONTENT_W,MARGIN,VIEW_HEIGHT
    U=ui.U;left,top,width,height=CHAPTER_PICTURE
    picture_top=TALL_SKILL_TOP
    tall.block(('chapter-shot-back','chapter-shot','chapter-shot-border'),(U*left,U*top),(MARGIN,picture_top),TALL_GROW,shift)
    beside=MARGIN+U*width*TALL_GROW+24
    for index,ident in enumerate(('chapter_start','chapter_back')):
        tall.block((ident,),(tall.dp(ident,'left'),tall.dp(ident,'top')),(beside,picture_top+40+index*TALL_PLATE_PITCH),TALL_PLATES,shift)
    name_top=picture_top+U*height*TALL_GROW+12
    tall.block(('chapter-name',),(U*left,U*(top+height+5)),(MARGIN,name_top),TALL_GROW,shift)
    detail_top=name_top+U*18*TALL_GROW+4
    tall.block(('chapter-detail',),(U*left,U*(top+height+26)),(MARGIN,detail_top),TALL_GROW,shift)
    detail_h=TALL_CHAPTER_DETAIL_LINES*tall.dp('chapter-detail','line-height')*TALL_GROW
    for ident in ('chapter-name','chapter-detail'):
        tall.set(ident,'width',CONTENT_W)
    tall.set('chapter-detail','height',detail_h)
    list_w=CONTENT_W-40
    heads={'name':(tall.dp('chapter-head-level','left'),tall.dp('chapter-head-level','top')),
           'location':(tall.dp('chapter-head-status','left'),tall.dp('chapter-head-status','top'))}
    heads_top=detail_top+detail_h+24
    # The list runs to the prompt bar over the bottom band, its top in view dp.
    lines=len(SAVE_LINES);list_top=heads_top+lines*U*14*TALL_GROW+10
    height={'op':'-','args':[VIEW_HEIGHT,round(list_top+TALL_SKILL_BOTTOM,4)]}
    tall_list(tall,'chapter',SAVE_LINES,list_w,heads,heads_top,shift,height,{'name':'chapter-head-level','location':'chapter-head-status'})
